#include "shell.h"
#include <mitsuba/render/sceneloader.h>
#include <mitsuba/render/integrator.h>
#include <mitsuba/render/integrator2.h>
#include <mitsuba/render/renderjob.h>
#include <mitsuba/core/plugin.h>
#include <mitsuba/core/thread.h>
#include <mitsuba/core/statistics.h>

int ProcessConfig::recommendedThreads() {
	return mitsuba::getCoreCount();
}
ProcessConfig ProcessConfig::resolveDefaults(ProcessConfig const& _cfg) {
	ProcessConfig cfg = _cfg;
	if (cfg.maxThreads < 0)
		cfg.maxThreads = recommendedThreads();
	return cfg;
}

namespace impl {

	struct Scene: ::Scene{
		Scene(mitsuba::Scene* scene) {
			this->scene = scene;
		}

		Scene(fs::pathstr const& path) {
			std::unique_ptr<mitsuba::SceneLoader> loader( new mitsuba::SceneLoader(mitsuba::SceneLoader::ParameterMap()) );
			this->scene = loader->load(path);
		}
	};

} // namespace

Scene* Scene::create(mitsuba::Scene* scene) {
	return new impl::Scene(scene);
}
Scene* Scene::load(fs::pathstr const& path) {
	return new impl::Scene(path);
}
Scene::~Scene() = default;

void Scene::printStats() {
	mitsuba::Statistics::getInstance()->printStats();
}

mitsuba::ref<mitsuba::Sensor> Scene::createModifiedSensor(mitsuba::Properties const& properties, mitsuba::Sensor& currentSensor, mitsuba::Sampler* sampler, mitsuba::Film* film) {
	mitsuba::ref<mitsuba::PluginManager> pluginMgr = mitsuba::PluginManager::getInstance();
	mitsuba::ref<mitsuba::Sensor> newSensor = static_cast<mitsuba::Sensor *>(
		pluginMgr->createObject(MTS_CLASS(mitsuba::Sensor), properties)
		);
	newSensor->addChild(sampler ? sampler : currentSensor.getSampler());
	newSensor->addChild(film ? film : currentSensor.getFilm());
	newSensor->setMedium(currentSensor.getMedium());
	newSensor->setWorldTransform( new mitsuba::AnimatedTransform(currentSensor.getWorldTransform()) );
	newSensor->configure();
	return newSensor;
}

mitsuba::ref<mitsuba::Sensor> Scene::cloneSensor(mitsuba::Sensor& currentSensor, mitsuba::Sampler* sampler, mitsuba::Film* film) {
	return createModifiedSensor(currentSensor.getProperties(), currentSensor, sampler, film);
}

mitsuba::ref<mitsuba::Integrator> Scene::cloneIntegrator(mitsuba::Integrator const& oldIntegrator) {
	mitsuba::ref<mitsuba::PluginManager> pluginMgr = mitsuba::PluginManager::getInstance();
	mitsuba::ref<mitsuba::Integrator> integrator = static_cast<mitsuba::Integrator *>(
		pluginMgr->createObject(MTS_CLASS(mitsuba::Integrator), oldIntegrator.getProperties())
		);
	int idx = 0;
	while (mitsuba::Integrator const* oldChild = oldIntegrator.getSubIntegrator(idx)) {
		mitsuba::ref<mitsuba::Integrator> child = cloneIntegrator(*oldChild);
		integrator->addChild(child);
		child->setParent(integrator);
		idx++;
	}
	integrator->configure();
	return integrator;
}

mitsuba::ref<mitsuba::Sampler> Scene::cloneSampler(mitsuba::Sampler const& sampler) {
	mitsuba::ref<mitsuba::PluginManager> pluginMgr = mitsuba::PluginManager::getInstance();
	mitsuba::ref<mitsuba::Sampler> newSampler = static_cast<mitsuba::Sampler *>(
		pluginMgr->createObject(MTS_CLASS(mitsuba::Sampler), sampler.getProperties())
		);
	newSampler->configure();
	return newSampler;
}

mitsuba::ref<mitsuba::Scene> Scene::clonePreprocessed(mitsuba::Scene& oldScene) {
	mitsuba::ref<mitsuba::Integrator> integrator = cloneIntegrator(*oldScene.getIntegrator());
	mitsuba::ref<mitsuba::Sampler> sampler = cloneSampler(*oldScene.getSampler());
	mitsuba::ref<mitsuba::Sensor> sensor = cloneSensor(*oldScene.getSensor(), sampler);
	mitsuba::ref<mitsuba::Scene> scene = new mitsuba::Scene(&oldScene);
	scene->setIntegrator(integrator);
	scene->addSensor(sensor);
	scene->setSensor(sensor);
	scene->setSampler(sampler);
	scene->removeSensor(oldScene.getSensor());
	scene->setScenePreprocessed(true);
	scene->configure();
	return scene;
}

std::vector<std::string> Scene::availablePlugins(char const* symbol, bool refresh) {
	static std::map<std::string, std::vector<std::string>> plugin_c;
	auto& plugins = plugin_c[symbol];
	if (refresh || plugins.empty()) {
		plugins = mitsuba::PluginManager::getInstance()->getAvailablePlugins(symbol);
		std::cout << "Refreshing plugins for \"" << symbol << "\": ";
		for (auto& p : plugins)
			std::cout << p << ", ";
		std::cout << "End of plugins\n";
	}
	return plugins;
}

mitsuba::ref<mitsuba::ConfigurableObject> Scene::createTemplate(mitsuba::Properties const& properties, mitsuba::Class const* type) {
	mitsuba::ref<mitsuba::PluginManager> pluginMgr = mitsuba::PluginManager::getInstance();
	if (!type)
		type = MTS_CLASS(mitsuba::ConfigurableObject);
	return pluginMgr->createObject(type, properties);
}

namespace impl {

	struct SceneProcess: ::SceneProcess {
		mitsuba::ref<mitsuba::Bitmap> framebuffer;

		mitsuba::ref<mitsuba::RenderQueue> currentQueue;
		mitsuba::ref<mitsuba::RenderJob> currentJob;

		bool isCancelled = false;

		SceneProcess(mitsuba::Scene* scene) {
			this->scene = scene;

			this->resolution = scene->getFilm()->getCropSize();
			this->framebuffer = new mitsuba::Bitmap(mitsuba::Bitmap::ERGBA, mitsuba::Bitmap::EFloat32, this->resolution);
			this->imageData = framebuffer->getFloat32Data();
		}
		~SceneProcess() {
			clean();
		}

		void clean() {
			if (currentJob) {
				currentJob->cancel();
				currentJob = nullptr;
			}
			if (currentQueue) {
				pause(false);
				currentQueue->waitLeft(0);
				currentQueue = nullptr;
			}
			this->isCancelled = false;
		}

		void renderAsync(int volatile* filmRevision, int maxThreads) override {
			pause(false);
			clean();

			currentQueue = new mitsuba::RenderQueue();
			struct Listener : mitsuba::RenderListener {
				mitsuba::Film* film;
				mitsuba::Bitmap* framebuffer;
				int volatile* filmRevision;

				void updateImage() {
					film->develop(mitsuba::Point2i(0, 0), framebuffer->getSize(), mitsuba::Point2i(0, 0), framebuffer);
					++*filmRevision;
				}

				void workEndEvent(const mitsuba::RenderJob *job, const mitsuba::ImageBlock *wr, bool cancelled) override {
					updateImage();
				}
				void refreshEvent(const mitsuba::RenderJob *job) override {
					updateImage();
				}
				void finishJobEvent(const mitsuba::RenderJob *job, bool cancelled) override {
					updateImage();
				}
			};
			mitsuba::ref<Listener> listener = new Listener;
			listener->film = this->scene->getFilm();
			listener->framebuffer = this->framebuffer;
			listener->filmRevision = filmRevision;
			currentQueue->registerListener(listener);

			mitsuba::Scheduler* sched = mitsuba::Scheduler::getInstance();
			int numThreads = sched->getWorkerCount();
			if (maxThreads > 0 && maxThreads < numThreads) {
				numThreads = maxThreads;
				sched->limitWorkersPerProcess(maxThreads);
			}
			this->numActiveThreads = numThreads;

			mitsuba::Statistics::getInstance()->resetAll();

			currentJob = new mitsuba::RenderJob("rend", scene, currentQueue, -1, -1, -1, false, true);
			currentJob->start();
		}
		void wait() override {
			currentJob->join();
		}

		bool running() override {
			return currentJob && currentJob->isRunning() && !paused();
		}

		void pause(bool pause) override {
			mitsuba::Scheduler* sched = mitsuba::Scheduler::getInstance();
			if (pause == !sched->isRunning())
				return;
			if (pause)
				sched->pause();
			else
				sched->start();
		}
		bool paused() const {
			return !mitsuba::Scheduler::getInstance()->isRunning();
		}

		void cancel() override {
			this->isCancelled = true;
			currentJob->cancel();
		}
		bool cancelled() const override {
			return this->isCancelled;
		}
	};

} // namespace

SceneProcess* SceneProcess::create(mitsuba::Scene* scene) {
	return new impl::SceneProcess(scene);
}
SceneProcess::~SceneProcess() = default;

#include <mitsuba/core/thread.h>

void register_mitsuba_thread(mitsuba::Thread* parent, char const* name = "im-mts") {
	mitsuba::Thread* thread = mitsuba::Thread::registerUnmanagedThread(name);
	thread->setLogger(parent->getLogger());
	thread->setFileResolver(parent->getFileResolver());
}

#include <random>
#include <algorithm>
#include <condition_variable>

#define ATOMIC_SPLAT

namespace impl {

	struct InteractiveSceneProcess: ::InteractiveSceneProcess{
		mitsuba::ref<mitsuba::Sampler> samplerPrototype;

		struct PauseSync {
			std::mutex mutex;
			std::condition_variable condition;
		} pause_sync;

		// per worker
		mitsuba::ref_vector<mitsuba::Sampler> samplers;
		mitsuba::ref_vector<mitsuba::ImageBlock> framebuffers;
		std::vector<float volatile*> frambufferData;

		mitsuba::ref_vector<mitsuba::ImageBlock> framebuffersDouble;

		mitsuba::ref_vector<mitsuba::Thread> workers;

		InteractiveSceneProcess(mitsuba::Scene* scene, mitsuba::Sampler* sampler, mitsuba::ResponsiveIntegrator* integrator, ProcessConfig const& config) {
			this->scene = scene;
			this->integrator = integrator;
			this->numActiveThreads = 0;
			this->paused = true;

			this->maxThreads =  mitsuba::getCoreCount();
			if (config.maxThreads > 0 && config.maxThreads < maxThreads)
				maxThreads = config.maxThreads;

			this->samplerPrototype = Scene::cloneSampler(*sampler);
			this->samplers.resize(maxThreads);
			for (int i = 0; i < maxThreads; ++i) {
				samplers[i] = this->samplerPrototype->clone();
			}

			mitsuba::Vector2i filmSize = scene->getFilm()->getSize();

			for (int i = 0; i < 1 + int(config.doubleBuffered); ++i) {
				if (i)
					swap(this->framebuffersDouble, this->framebuffers);
				this->framebuffers.resize(maxThreads);
#ifdef ATOMIC_SPLAT
				framebuffers[0] = new mitsuba::ImageBlock(mitsuba::Bitmap::ERGBA, filmSize, scene->getFilm()->getReconstructionFilter());
				for (int i = 1; i < maxThreads; ++i)
					framebuffers[i] = framebuffers[0];
				this->uniqueTargets = 1;
#else
				for (int i = 0; i < maxThreads; ++i)
					framebuffers[i] = new mitsuba::ImageBlock(mitsuba::Bitmap::ERGBA, filmSize, scene->getFilm()->getReconstructionFilter());
				this->uniqueTargets = maxThreads;
#endif
			}

			this->frambufferData.resize(maxThreads);
			for (int i = 0; i < maxThreads; ++i) {
				frambufferData[i] = framebuffers[i]->getBitmap()->getFloatData();
				this->resolution = framebuffers[i]->getBitmap()->getSize();
			}
			this->imageData = this->frambufferData.data();

			integrator->allocate(*scene, (mitsuba::Sampler*const*) samplers.data(), (mitsuba::ImageBlock*const*) framebuffers.data(), maxThreads);
		}

		void pause(bool pause) override {
			{ // lock b/c need to allow atomic check & wait
				std::lock_guard<std::mutex> lock(this->pause_sync.mutex);
				this->paused = pause;
			}
			if (!pause)
				this->pause_sync.condition.notify_all();
		}

		void render(mitsuba::Sensor* sensor, double volatile imageSamples[], Controls controls, int numThreads) override {
			if (numThreads < 0 || numThreads > this->maxThreads)
				numThreads = this->maxThreads;

			this->numActiveThreads = numThreads;
			this->paused = false;
			
#ifdef ATOMIC_SPLAT
			framebuffers[0]->clear();
#endif
			mitsuba::Statistics::getInstance()->resetAll();

			auto parallel_execution = [this, sensor, imageSamples, controls, numThreads](int tid) {
				mitsuba::Vector2i resolution = this->resolution;
				mitsuba::Sampler* sampler = this->samplers[tid];
				mitsuba::ImageBlock* block = this->framebuffers[tid];
				double volatile& spp = imageSamples[tid];

#ifndef ATOMIC_SPLAT
				block->clear();
#endif
				struct Interrupt : mitsuba::ResponsiveIntegrator::Interrupt {
					InteractiveSceneProcess* proc;
					float* imageData;
					volatile float *volatile& imageDataTarget;
					double volatile& sppTarget;

					Interrupt(InteractiveSceneProcess* proc, float* imageData, volatile float *volatile& imageDataTarget, double volatile& spp)
						: proc(proc), imageData(imageData), imageDataTarget(imageDataTarget), sppTarget(spp) { }

					int progress(mitsuba::ResponsiveIntegrator* integrator, const mitsuba::Scene &scene, const mitsuba::Sensor &sensor, mitsuba::Sampler &sampler, mitsuba::ImageBlock& target, double spp
						, mitsuba::ResponsiveIntegrator::Controls controls, int threadIdx, int threadCount) override {
						if (spp) {
							imageDataTarget = imageData;
							sppTarget = spp;
						}

						if (proc->paused) {
							std::unique_lock<std::mutex> lock(proc->pause_sync.mutex);
							while (proc->paused && !(controls.continu && !*controls.continu) && !(controls.abort && *controls.abort))
								proc->pause_sync.condition.wait(lock);
						}

						return 0;
					}
				} interrupt = { this, block->getBitmap()->getFloatData(), this->imageData[tid], spp };

				struct mitsuba::ResponsiveIntegrator::Controls icontrols = {
					controls.continu,
					controls.abort,
					&interrupt
				};

				this->integrator->render(*this->scene, *sensor, *sampler, *block, icontrols, tid, numThreads);

				// end of parallel execution
			};

			// build on mitsuba infrastructure instead of OpenMP b/c for classic mitsuba thread-local support etc.
			while (numThreads > (int) workers.size()) {
				int i = (int) workers.size();
				typedef decltype(parallel_execution) par_exe;
				struct Thread : mitsuba::Thread {
					int tid;
					par_exe& parallel_execution;
					Thread(int tid, par_exe& parallel_execution)
						: mitsuba::Thread("interactive")
						, tid(tid)
						, parallel_execution(parallel_execution) { }
					void run() override {
						parallel_execution(tid);
					}
				};
				workers.push_back(new Thread(i, parallel_execution));
			}
			for (int i = 0; i < numThreads; ++i) {
				workers[i]->start();
			}
			for (int i = numThreads; i-- > 0; ) {
				workers[i]->join();
			}

			// don't change the contents until next samples are ready, if double buffered
			if (!framebuffersDouble.empty()) {
				bool hadRevisions = false;
				for (int i = 0; i < numThreads; ++i)
					hadRevisions |= bool(imageSamples[i]);
				if (hadRevisions)
					swap(framebuffers, framebuffersDouble);
			}
		}
	};

} // namespace

static mitsuba::ref<mitsuba::Integrator> makePathTracer(mitsuba::Properties const& properties) {
	mitsuba::Properties pathTracerProps(properties);
	pathTracerProps.setPluginName("path");

	mitsuba::ref<mitsuba::PluginManager> pluginMgr = mitsuba::PluginManager::getInstance();
	mitsuba::ref<mitsuba::Integrator> newIntegrator = static_cast<mitsuba::Integrator *>(
		pluginMgr->createObject(MTS_CLASS(mitsuba::Integrator), pathTracerProps)
		);

	newIntegrator->configure();
	return newIntegrator;
}

InteractiveSceneProcess* InteractiveSceneProcess::create(mitsuba::Scene* scene, mitsuba::Sampler* sampler, mitsuba::ResponsiveIntegrator* integrator, ProcessConfig const& config) {
	return new impl::InteractiveSceneProcess(scene, sampler, integrator, config);
}
InteractiveSceneProcess* InteractiveSceneProcess::create(mitsuba::Scene* scene, mitsuba::Sampler* sampler, mitsuba::Integrator* integrator, ProcessConfig const& config) {
	mitsuba::ref<mitsuba::ResponsiveIntegrator> rintegrator = integrator->makeResponsiveIntegrator();
	if (!rintegrator) {
		SLog(mitsuba::EInfo, "Creating default path integrator ('%s' does not support responsive preview)", integrator->getProperties().getPluginName().c_str());
		mitsuba::ref<mitsuba::Integrator> pt = makePathTracer(integrator->getProperties());
		rintegrator = pt->makeResponsiveIntegrator();
	}
	return create(scene, sampler, rintegrator, config);
}
InteractiveSceneProcess::~InteractiveSceneProcess() = default;

#include <thread>

namespace impl {

	struct WorkLane: ::WorkLane{
		Worker* worker;

		mutable std::mutex mutex;
		mutable std::condition_variable condition;

		mutable int volatile awaiting_sync = false;
		mutable int is_sync = false;

		std::thread thread;

		static void run(WorkLane* lane, mitsuba::Thread* parentThread) {
			register_mitsuba_thread(parentThread, "im-lane");
			Worker* worker = lane->worker;
			lane->started = true;
			while (lane->continu) {
				worker->work(lane);
				std::this_thread::yield();
			}
			lane->stopped = true;
		}

		WorkLane(Worker* worker)
			: worker(worker)
			, thread(run, this, mitsuba::Thread::getThread()) {
		}
		~WorkLane() {
			continu = false;
			worker->quit(this);
			thread.join();
		}

		int synchronized(Sync& sync) const override {
			std::unique_lock<std::mutex> lock(mutex);
			++awaiting_sync;
			while (!is_sync) {
				condition.wait(lock);
			}
			--awaiting_sync;

			int r = sync.sync();
			condition.notify_all();
			return r;
		}
		void synchronize() const override {
			if (awaiting_sync) {
				std::unique_lock<std::mutex> lock(mutex);
				is_sync = true;
				while (awaiting_sync) {
					condition.notify_all();
					condition.wait(lock);
				}
				is_sync = false;
			}
		}
	};

} // namespace

WorkLane* WorkLane::create(Worker* worker) {
	return new impl::WorkLane(worker);
}
WorkLane::~WorkLane() = default;
