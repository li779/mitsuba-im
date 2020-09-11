#include "im_render.h"
#include <mitsuba/render/integrator.h>
#include <mitsuba/render/integrator2.h>
#include <mitsuba/render/sampler.h>
#include <mitsuba/render/imageblock.h>
#include <mitsuba/render/scene.h>
#include <mitsuba/core/plugin.h>
#include <mitsuba/core/thread.h>
#include <mitsuba/core/fresolver.h>
#include <mitsuba/core/statistics.h>
#include <cstdlib>

int ProcessConfig::recommendedThreads() {
	return mitsuba::getCoreCount();
}
ProcessConfig ProcessConfig::resolveDefaults(ProcessConfig const& _cfg) {
	ProcessConfig cfg = _cfg;
	if (cfg.maxThreads <= 0)
		cfg.maxThreads = recommendedThreads();
	return cfg;
}

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
#define CORES_PER_FRAMEBUFFER 8

namespace impl {

	struct InteractiveSceneProcess: ::InteractiveSceneProcess{
		mitsuba::ref<mitsuba::Sampler> samplerPrototype;

		// per worker
		mitsuba::ref_vector<mitsuba::Sampler> samplers;
		mitsuba::ref_vector<mitsuba::ImageBlock> framebuffers;
		std::vector<float volatile*> frambufferData;

		mitsuba::ref_vector<mitsuba::Thread> workers;

		bool updateSamplersAndIntegrator() {
			for (auto& s : samplers) {
				s = this->samplerPrototype->clone();
			}

			return integrator->allocate(*scene, (mitsuba::Sampler*const*) samplers.data(), (mitsuba::ImageBlock*const*) framebuffers.data(), maxThreads);
		}

		InteractiveSceneProcess(mitsuba::Scene* scene, mitsuba::Sampler* sampler, mitsuba::ResponsiveIntegrator* integrator, ProcessConfig const& config) {
			this->scene = scene;
			this->integrator = integrator;
			this->numActiveThreads = 0;

			this->maxThreads =  mitsuba::getCoreCount();
			if (config.maxThreads > 0 && config.maxThreads < maxThreads)
				maxThreads = config.maxThreads;

			this->samplerPrototype = sampler;
			this->samplers.resize(maxThreads);

			mitsuba::Vector2i filmSize = scene->getFilm()->getSize();
			{
				this->framebuffers.resize(maxThreads);
#ifdef ATOMIC_SPLAT
				this->uniqueTargets = 0;
				for (int i = 0; i < maxThreads; ++i) {
					if (i % CORES_PER_FRAMEBUFFER == 0) {
						framebuffers[i] = new mitsuba::ImageBlock(mitsuba::Bitmap::ERGBA, filmSize, scene->getFilm()->getReconstructionFilter());
						++this->uniqueTargets;
					}
					else
						framebuffers[i] = framebuffers[i - 1];
				}
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

			updateSamplersAndIntegrator();
		}

		void render(mitsuba::Sensor* sensor, double volatile imageSamples[], Controls controls, int numThreads) override {
			if (numThreads < 0 || numThreads > this->maxThreads)
				numThreads = this->maxThreads;

			this->numActiveThreads = numThreads;
			
#ifdef ATOMIC_SPLAT
			for (int i = 0; i < numThreads; ++i)
				if (i % CORES_PER_FRAMEBUFFER == 0)
					framebuffers[i]->clear();
#endif

			mitsuba::Statistics::getInstance()->resetAll();

			volatile int returnCode = 0;
			bool initialRun = true;
			auto parallel_execution = [this, sensor, imageSamples, controls, numThreads, &returnCode, &initialRun](int tid) {
				mitsuba::Vector2i resolution = this->resolution;
				mitsuba::Sampler* sampler = this->samplers[tid];
				mitsuba::ImageBlock* block = this->framebuffers[tid];
				double volatile& spp = imageSamples[tid];

				if (initialRun) {
#ifndef ATOMIC_SPLAT
					block->clear();
#endif
				}

				struct Interrupt : mitsuba::ResponsiveIntegrator::Interrupt {
					struct InterruptM {
						InteractiveSceneProcess* proc;
						float* imageData;
						volatile float *volatile& imageDataTarget;
						double volatile& sppTarget;
						int maxSpp;
					} m;
					Interrupt(InterruptM const & m) : m(m) { }

					int progress(mitsuba::ResponsiveIntegrator* integrator, const mitsuba::Scene &scene, const mitsuba::Sensor &sensor, mitsuba::Sampler &sampler, mitsuba::ImageBlock& target, double spp
						, mitsuba::ResponsiveIntegrator::Controls controls, int threadIdx, int threadCount) override {
						if (spp) {
							m.imageDataTarget = m.imageData;
							m.sppTarget = spp;
						}
						if (spp * threadCount >= (double) m.maxSpp) {
							SLog(mitsuba::EInfo, "Integrator keeps going, halting at max sample count");
							return 100;
						}
						return 0;
					}
				} interrupt = { { this, block->getBitmap()->getFloatData(), this->imageData[tid], spp, (int) sampler->getSampleCount() } };

				struct mitsuba::ResponsiveIntegrator::Controls icontrols = {
					controls.continu,
					controls.abort,
					&interrupt
				};

				int rc = this->integrator->render(*this->scene, *sensor, *sampler, *block, icontrols, tid, numThreads);
				if (rc)
					returnCode = rc;

				// end of parallel execution
			};

			// build on mitsuba infrastructure instead of OpenMP b/c for classic mitsuba thread-local support etc.
			if (numThreads > (int) workers.size())
				workers.resize(numThreads);
			for (int i = 0; i < numThreads; ++i) {
				typedef decltype(parallel_execution) par_exe;
				struct Thread : mitsuba::Thread {
					int tid;
					par_exe* parallel_execution;
					Thread(int tid, par_exe* parallel_execution)
						: mitsuba::Thread("interactive")
						, tid(tid)
						, parallel_execution(parallel_execution) { setCoreAffinity(tid); }
					void run() override {
						(*parallel_execution)(tid);
					}
				};
				if (auto* w = workers[i].get())
					((Thread*) w)->parallel_execution = &parallel_execution;
				else
					workers[i] = new Thread(i, &parallel_execution);
			}


			for (int i = 0; i < numThreads; ++i) {
				workers[i]->start();
			}
			for (int i = numThreads; i-- > 0; ) {
				workers[i]->join();
			}
		}

		void render(int numThreads) override {
			scene->getFilm()->setDestinationFile(scene->getDestinationFile(), scene->getBlockSize());
			scene->preprocess(nullptr, nullptr, -1, -1, -1); // todo: this might crash for more advanced subsurf integrators ...?

			std::vector<double> spps(maxThreads);
			Controls ctrl = { };
			render(scene->getSensor(), spps.data(), ctrl, numThreads);
			numThreads = this->numActiveThreads;

			{
				double spp = 0.0f;
				for (double s : spps)
					spp += s;

				SLog(mitsuba::EInfo, "SPP: %f", spp);

				mitsuba::ref<mitsuba::ImageBlock> developBuffer = new mitsuba::ImageBlock(mitsuba::Bitmap::ERGBA, scene->getFilm()->getCropSize());
				developBuffer->clear();
				for (int i = 0; i < numThreads; ++i)
					if (i % CORES_PER_FRAMEBUFFER == 0)
						developBuffer->put(framebuffers[i]);
				float* data = developBuffer->getBitmap()->getFloatData();
				for (size_t i = 0, ie = developBuffer->getBitmap()->getPixelCount() * developBuffer->getBitmap()->getChannelCount(); i < ie; ++i) {
					*data = float(*data / spp);
					++data;
				}
				scene->getFilm()->setBitmap(developBuffer->getBitmap());
			}

			scene->postprocess(nullptr, nullptr, -1, -1, -1); // todo: this might crash for more advanced subsurf integrators ...?
		}
	};

} // namespace

InteractiveSceneProcess* InteractiveSceneProcess::create(mitsuba::Scene* scene, mitsuba::Sampler* sampler, mitsuba::ResponsiveIntegrator* integrator, ProcessConfig const& config) {
	return new impl::InteractiveSceneProcess(scene, sampler, integrator, config);
}
InteractiveSceneProcess* InteractiveSceneProcess::create(mitsuba::Scene* scene, mitsuba::Sampler* sampler, mitsuba::Integrator* integrator, ProcessConfig const& config) {
	mitsuba::ref<mitsuba::ResponsiveIntegrator> rintegrator = integrator->makeResponsiveIntegrator();
	if (!rintegrator) {
		SLog(mitsuba::EInfo, "Using standard integrator ('%s' does not support responsive preview)", integrator->getProperties().getPluginName().c_str());
		return nullptr;
	}
	return create(scene, sampler, rintegrator, config);
}
InteractiveSceneProcess::~InteractiveSceneProcess() = default;
