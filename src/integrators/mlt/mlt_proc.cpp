/*
    This file is part of Mitsuba, a physically based rendering system.

    Copyright (c) 2007-2014 by Wenzel Jakob and others.

    Mitsuba is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License Version 3
    as published by the Free Software Foundation.

    Mitsuba is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <mitsuba/bidir/mut_bidir.h>
#include <mitsuba/bidir/mut_lens.h>
#include <mitsuba/bidir/mut_caustic.h>
#include <mitsuba/bidir/mut_mchain.h>
#include <mitsuba/bidir/mut_manifold.h>
#include <mitsuba/bidir/util.h>
#include <mitsuba/render/integrator2.h>
#include "mlt_proc.h"

MTS_NAMESPACE_BEGIN

//#define MTS_BD_DEBUG_HEAVY

static StatsCounter statsAccepted("Path Space MLT",
		"Accepted mutations", EPercentage);
static StatsCounter forcedAcceptance("Path Space MLT",
		"Number of forced acceptances");

/* ==================================================================== */
/*                         Worker implementation                        */
/* ==================================================================== */

class MLTRenderer : public WorkProcessor {
public:
	MLTRenderer(const MLTConfiguration &conf)
		: m_config(conf) {
	}

	MLTRenderer(Stream *stream, InstanceManager *manager)
		: WorkProcessor(stream, manager) {
		m_config = MLTConfiguration(stream);
	}

	void serialize(Stream *stream, InstanceManager *manager) const {
		m_config.serialize(stream);
	}

	ref<WorkUnit> createWorkUnit() const {
		return new SeedWorkUnit();
	}

	ref<WorkResult> createWorkResult() const {
		return new ImageBlock(Bitmap::ESpectrum,
			m_film->getCropSize(), m_film->getReconstructionFilter());
	}

	void prepare() {
		Scene *scene = static_cast<Scene *>(getResource("scene"));
		m_sampler = static_cast<Sampler *>(getResource("sampler"));
		m_sensor = static_cast<Sensor *>(getResource("sensor"));
		m_scene = new Scene(scene);
		m_film = m_sensor->getFilm();
		m_scene->setSensor(m_sensor);
		m_scene->setSampler(m_sampler);
		m_scene->removeSensor(scene->getSensor());
		m_scene->addSensor(m_sensor);
		m_scene->setSensor(m_sensor);
		m_scene->wakeup(NULL, m_resources);
		m_scene->initializeBidirectional();

		m_rplSampler = static_cast<ReplayableSampler*>(
			static_cast<Sampler *>(getResource("rplSampler"))->clone().get());

		m_currentImageWeight = nullptr;

		prepareAlways();
	}

	void prepareResponsive(Scene const* scene, Sensor const* sensor, Sampler* sampler, ImageBlock* result, ReplayableSampler* seedSampler, Float *currentImageWeight) {
		m_scene = const_cast<Scene*>(scene);
		m_sampler = sampler;
		m_sensor = const_cast<Sensor *>(sensor);
		m_film = m_sensor->getFilm();
		
		m_rplSampler = seedSampler;

		m_currentImageWeight = currentImageWeight;

		prepareAlways();
	}

	void prepareAlways() {
		m_pathSampler = new PathSampler(PathSampler::EBidirectional, m_scene,
			m_rplSampler, m_rplSampler, m_rplSampler, m_config.maxDepth, 10,
			m_config.separateDirect, true);

		m_pool = &m_pathSampler->getMemoryPool();

		/* Jump sizes recommended by Eric Veach */
		Float minJump = 0.1f, coveredArea = 0.05f;

		/* Register all available mutators */
		if (m_config.bidirectionalMutation)
			m_mutators.push_back(new BidirectionalMutator(m_scene, m_sampler,
				*m_pool, m_config.separateDirect ? 5 : 3,
				m_config.maxDepth == -1 ? INT_MAX : m_config.maxDepth + 2));

		if (m_config.lensPerturbation)
			m_mutators.push_back(new LensPerturbation(m_scene, m_sampler, *m_pool,
				minJump, coveredArea));

		if (m_config.multiChainPerturbation)
			m_mutators.push_back(new MultiChainPerturbation(m_scene, m_sampler, *m_pool,
				minJump, coveredArea));

		if (m_config.causticPerturbation)
			m_mutators.push_back(new CausticPerturbation(m_scene, m_sampler, *m_pool,
				minJump, coveredArea));

		if (m_config.manifoldPerturbation)
			m_mutators.push_back(new ManifoldPerturbation(m_scene, m_sampler, *m_pool,
				m_config.probFactor, true, true));

		if (m_mutators.size() == 0)
			Log(EError, "There must be at least one mutator!");
	}

	void process(const WorkUnit *workUnit, WorkResult *workResult, const bool &stop) {
		ImageBlock *result = static_cast<ImageBlock *>(workResult);
		const SeedWorkUnit *wu = static_cast<const SeedWorkUnit *>(workUnit);
		Path *current = new Path(), *proposed = new Path();
		Spectrum relWeight(0.0f);
		Float* currentImageWeight = m_currentImageWeight;

		// classic mitusba mode
		if (m_config.luminanceSamples != 0) {
			result->clear();
		}

		/// Reconstruct the seed path
		m_pathSampler->reconstructPath(wu->getSeed(), m_config.importanceMap, *current);
		relWeight = current->getRelativeWeight();
		BDAssert(!relWeight.isZero());

		DiscreteDistribution suitabilities(m_mutators.size());
		MutationRecord muRec, currentMuRec(Mutator::EMutationTypeCount,0,0,0,Spectrum(0.f));
		ref<Timer> timer = new Timer();

		size_t consecRejections = 0;
		Float accumulatedWeight = 0;

		#if defined(MTS_DEBUG_FP)
			enableFPExceptions();
		#endif

		#if defined(MTS_BD_DEBUG_HEAVY)
			std::ostringstream oss;
			Path backup;
		#endif
		for (size_t mutationCtr=0; mutationCtr < m_config.nMutations
				&& !stop; ++mutationCtr) {
			if (wu->getTimeout() > 0 && (mutationCtr % 8192) == 0 &&
					(int) timer->getMilliseconds() > wu->getTimeout())
				break;

			/* Query all mutators for their suitability */
			suitabilities.clear();
			for (size_t j=0; j<m_mutators.size(); ++j)
				suitabilities.append(m_mutators[j]->suitability(*current));
			#if defined(MTS_BD_DEBUG_HEAVY)
				current->clone(backup, *m_pool);
			#endif

			size_t mutatorIdx = 0;
			bool success = false;
			Mutator *mutator = NULL;

			if (suitabilities.normalize() == 0) {
				/* No mutator can handle this path -- give up */
				size_t skip = m_config.nMutations - mutationCtr;
				accumulatedWeight += skip;
				consecRejections += skip;
				break;
			}

			mutatorIdx = suitabilities.sample(m_sampler->next1D());
			mutator = m_mutators[mutatorIdx].get();

			/* Sample a mutated path */
			success = mutator->sampleMutation(*current, *proposed, muRec, currentMuRec);

			#if defined(MTS_BD_DEBUG_HEAVY)
				if (backup != *current)
					Log(EError, "Detected an unexpected path modification after a "
						"mutation of type %s (k=%i)!", muRec.toString().c_str(),
						current->length());
				if (success) {
					bool fail = false;
					for (int i=0; i<muRec.l; ++i)
						if (*backup.vertex(i) != *proposed->vertex(i))
							fail = true;

					for (int i=1; i <= backup.length() - muRec.m; ++i)
						if (*backup.vertex(muRec.m+i) != *proposed->vertex(muRec.l+muRec.ka+i))
							fail = true;
					if (fail)
						Log(EError, "Detected an unexpected path modification outside of the "
							"specified range after a mutation of type %s (k=%i)!",
							muRec.toString().c_str(), current->length());
				}
				backup.release(*m_pool);
			#endif

			statsAccepted.incrementBase(1);
			if (success) {
				Float Qxy = mutator->Q(*current, *proposed, muRec) * suitabilities[mutatorIdx];
				suitabilities.clear();
				for (size_t j=0; j<m_mutators.size(); ++j)
					suitabilities.append(m_mutators[j]->suitability(*proposed));
				suitabilities.normalize();
				Float Qyx = mutator->Q(*proposed, *current, muRec.reverse()) * suitabilities[mutatorIdx];

				Float a;
				if (!m_config.importanceMap) {
					if(Qxy > RCPOVERFLOW)
					a = std::min((Float) 1, Qyx / Qxy);
					else
						a = 0.f;
				} else {
					const Float *luminanceValues = m_config.importanceMap->getFloatData();
					const Point2 &curPos = current->getSamplePosition();
					const Point2 &propPos = proposed->getSamplePosition();
					Vector2i size = m_config.importanceMap->getSize();
					Point2i curPosI(
						std::min(std::max(0, (int) curPos.x), size.x-1),
						std::min(std::max(0, (int) curPos.y), size.y-1));
					Point2i propPosI(
						std::min(std::max(0, (int) propPos.x), size.x-1),
						std::min(std::max(0, (int) propPos.y), size.y-1));

					Float curValue = luminanceValues[curPosI.x + curPosI.y * size.x];
					Float propValue = luminanceValues[propPosI.x + propPosI.y * size.x];

					a = std::min((Float) 1, (Qyx * curValue) / (Qxy * propValue));
				}

				#if defined(MTS_BD_DEBUG_HEAVY)
					if (!proposed->verify(m_scene, EImportance, oss)) {
						Log(EWarn, "%s proposed as %s, Qxy=%f, Qyx=%f", oss.str().c_str(),
								muRec.toString().c_str(), Qxy, Qyx);
						proposed->release(muRec.l, muRec.l + muRec.ka + 1, *m_pool);
						oss.str("");
						continue;
					}
				#endif

				if (Qxy == 0) { // be tolerant of this (can occasionally happen due to floating point inaccuracies)
					a = 0;
				} else if (Qxy < 0 || Qyx < 0 || std::isnan(Qxy) || std::isnan(Qyx)) {
					#if defined(MTS_BD_DEBUG)
						Log(EDebug, "Source path: %s", current->toString().c_str());
						Log(EDebug, "Proposal path: %s", proposed->toString().c_str());
						Log(EWarn, "Internal error while computing acceptance probabilities: "
							"Qxy=%f, Qyx=%f, muRec=%s", Qxy, Qyx, muRec.toString().c_str());
					#endif
					a = 0;
				}

				accumulatedWeight += 1-a;

				/* Accept with probability 'a' */
				if (a == 1 || m_sampler->next1D() < a) {
					current->release(muRec.l, muRec.m+1, *m_pool);
					Spectrum value = relWeight * accumulatedWeight;
					if (currentImageWeight)
						value *= *currentImageWeight;
					if (!value.isZero())
						result->putAtomic(current->getSamplePosition(), &value[0]);

					/* The mutation was accepted */
					std::swap(current, proposed);
					relWeight = current->getRelativeWeight();
					mutator->accept(muRec);
					currentMuRec = muRec;
					accumulatedWeight = a;
					consecRejections = 0;
					++statsAccepted;
				} else {
					/* The mutation was rejected */
					proposed->release(muRec.l, muRec.l + muRec.ka + 1, *m_pool);
					consecRejections++;
					if (a > 0) {
						Spectrum value = proposed->getRelativeWeight() * a;
						if (currentImageWeight)
							value *= *currentImageWeight;
						result->putAtomic(proposed->getSamplePosition(), &value[0]);
					}
				}
			} else {
				accumulatedWeight += 1;
				consecRejections++;
			}
		}
		#if defined(MTS_BD_DEBUG)
			if (consecRejections == m_config.nMutations)
				Log(EWarn, "Encountered a path that could *never* be mutated!: %s",
					current->toString().c_str());
		#endif

		if (accumulatedWeight > 0) {
			Spectrum value = relWeight * accumulatedWeight;
			if (currentImageWeight)
				value *= *currentImageWeight;
			result->putAtomic(current->getSamplePosition(), &value[0]);
		}

		#if defined(MTS_DEBUG_FP)
			disableFPExceptions();
		#endif

		current->release(*m_pool);
		delete current;
		delete proposed;
		if (!m_pool->unused())
			Log(EError, "Internal error: detected a memory pool leak!");
	}

	class MLTResponsive : public ResponsiveIntegrator {
		#define SeedSamplesPerChain 32

		ref<Integrator> m_integrator;
		MLTConfiguration const* m_config;

		ref_vector<ReplayableSampler> m_seedSamplers;

	public:
		MLTResponsive(Integrator* mlt, MLTConfiguration const* config)
			: ResponsiveIntegrator(mlt->getProperties())
			, m_integrator(mlt)
			, m_config(config) {
		}

		bool preprocess(const Scene *scene, const Sensor* sensor, const Sampler* sampler) {
			return m_integrator->preprocess(scene, nullptr, nullptr, -1, -1, -1);
		}

		bool allocate(const Scene &scene, Sampler *const *samplers, ImageBlock *const *targets, int threadCount) override {
			bool result = ResponsiveIntegrator::allocate(scene, samplers, targets, threadCount);

			ref<Random> rnd = new Random();
			m_seedSamplers.clear();
			for (int i = 0; i < threadCount; ++i) {
				m_seedSamplers.push_back(new ReplayableSampler(rnd));
			}

			for (int i = 0; i < threadCount; ++i) {
				m_integrator->configureSampler(&scene, samplers[i]);
			}

			return result;
		}

		int render(const Scene &scene, const Sensor &sensor, Sampler &sampler, ImageBlock& target
			, Controls controls, int threadIdx, int threadCount) override {
			Vector2i pixels = target.getSize();
			int planeSamples = pixels.x * pixels.y;

			MLTConfiguration config = *m_config;
			config.luminance = 0.2f;
			config.luminanceSamples = 0;
			config.separateDirect = false;
			config.directSamples = 0;
			config.twoStage = false;
			config.firstStage = false;
			config.importanceMap = NULL;
			config.workUnits = 0;
			config.nMutations = std::max(4 * pixels.x * pixels.y / threadCount, 300000);

			struct MeanBrightness {
				Float value = 0;
				Float samples = 0;
			} meanImage;

			ref<MLTRenderer> renderer = new MLTRenderer(config);
			renderer->prepareResponsive(&scene, &sensor, &sampler, &target, m_seedSamplers[threadIdx], &meanImage.value);

			struct LargeStepEstimator : LargeStepTracker {
				MeanBrightness &meanImage;

				LargeStepEstimator(MeanBrightness &meanImage)
					: meanImage(meanImage) { }

				void proposedLargeStep(Float weight, Path const &path) override {
					float const onlineWeight = .1f; // higher variance
					meanImage.samples += onlineWeight;
					meanImage.value += (weight - meanImage.value) * onlineWeight / meanImage.samples;
				}
			} largeStepEstimator = { meanImage };
			for (Mutator* m : renderer->m_mutators)
				m->setLargeStepTracker(&largeStepEstimator, .05f);

			// todo: offset sampler per thread?
			ReplayableSampler* seedSampler = renderer->m_rplSampler;
			PathSampler* pathSampler = renderer->m_pathSampler;
			std::vector<PathSeed> pathSeeds;

			int currentSamples = 0, completedPlanes = 0;
			double spp = 0.0f;

			int returnCode = 0;
			while (returnCode == 0) {
				// update statistics and check control
				{
					spp = (double) completedPlanes + double(currentSamples + config.nMutations / 2) / double(planeSamples);

					// external control
					if (controls.abort && *controls.abort) {
						returnCode = -1;
					} else if (controls.continu && !*controls.continu) {
						returnCode = -2;
					} else if (controls.interrupt) {
						// important: always called on new plane begin!
						returnCode = controls.interrupt->progress(this, scene, sensor, sampler, target, spp, controls, threadIdx, threadCount);
					}
					if (returnCode != 0) {
						break;
					}
				}

				{
					size_t sampleIndex;
					Float totalSplat;
					PathSampler::PathCallback callback = [&pathSeeds, &sampleIndex, &totalSplat](int s, int t, Float w, Path &p) {
						if (w > 0.0f) {
							pathSeeds.push_back(PathSeed(sampleIndex, w, s, t));
							totalSplat += w;
							// todo: insert into dbor
						}
					};
					for (int i = 0; i < SeedSamplesPerChain; ) {
						size_t seedIndex = pathSeeds.size();
						sampleIndex = seedSampler->getSampleIndex();
						totalSplat = 0.0f;
						pathSampler->samplePaths(Point2i(-1), callback);
						meanImage.samples += 1.f;
						meanImage.value += (totalSplat - meanImage.value) / meanImage.samples;
						i += int(pathSeeds.size() - seedIndex);
					}
				}

				SeedWorkUnit swu;
				swu.setTimeout(0);
				Float totalSeedWeight = 0;
				for (PathSeed& s : pathSeeds) {
					Float seedWeight = s.luminance;
					Float prevSeedWeight = totalSeedWeight;
					totalSeedWeight += seedWeight;

					Float u = sampler.next1D();
					if (seedWeight < prevSeedWeight ? u < seedWeight / totalSeedWeight : u >= prevSeedWeight / totalSeedWeight) {
						swu.setSeed(s);
					}
				}
				bool stop = false;
				renderer->process(&swu, &target, controls.abort ? *(bool const*) controls.abort : stop);
				currentSamples += int(config.nMutations);

				// precise sample tracking
				while (currentSamples >= planeSamples) {
					++completedPlanes;
					currentSamples -= planeSamples;
					spp = (double) completedPlanes;
				}
			}

			return returnCode;
		}

		Float getLowerSampleBound() const override {
			return 0.0f;
		}
	};


	ref<WorkProcessor> clone() const {
		return new MLTRenderer(m_config);
	}

	MTS_DECLARE_CLASS()
private:
	MLTConfiguration m_config;
	ref<Sensor> m_sensor;
	ref<Film> m_film;
	ref<Scene> m_scene;
	ref<Sampler> m_sampler;
	ref<ReplayableSampler> m_rplSampler;
	ref<PathSampler> m_pathSampler;
	ref_vector<Mutator> m_mutators;
	MemoryPool *m_pool;
	Float *m_currentImageWeight;
};

/* ==================================================================== */
/*                           Parallel process                           */
/* ==================================================================== */

MLTProcess::MLTProcess(const RenderJob *parent, RenderQueue *queue,
	const MLTConfiguration &conf, const Bitmap *directImage,
	const std::vector<PathSeed> &seeds) : m_job(parent), m_queue(queue),
		m_config(conf), m_progress(NULL), m_seeds(seeds) {
	m_directImage = directImage;
	m_timeoutTimer = new Timer();
	m_refreshTimer = new Timer();
	m_resultMutex = new Mutex();
	m_resultCounter = 0;
	m_workCounter = 0;
	m_refreshTimeout = 1;
}

ref<WorkProcessor> MLTProcess::createWorkProcessor() const {
	return new MLTRenderer(m_config);
}

void MLTProcess::develop() {
	LockGuard lock(m_resultMutex);
	size_t pixelCount = m_accum->getBitmap()->getPixelCount();
	const Spectrum *accum = (Spectrum *) m_accum->getBitmap()->getData();
	const Spectrum *direct = m_directImage != NULL ?
		(Spectrum *) m_directImage->getData() : NULL;
	const Float *importanceMap = m_config.importanceMap != NULL ?
			m_config.importanceMap->getFloatData() : NULL;
	Spectrum *target = (Spectrum *) m_developBuffer->getData();

	/* Compute the luminance correction factor */
	Float avgLuminance = 0;
	if (importanceMap) {
		for (size_t i=0; i<pixelCount; ++i)
			avgLuminance += accum[i].getLuminance() * importanceMap[i];
	} else {
		for (size_t i=0; i<pixelCount; ++i)
			avgLuminance += accum[i].getLuminance();
	}

	avgLuminance /= (Float) pixelCount;
	Float luminanceFactor = m_config.luminance / avgLuminance;

	for (size_t i=0; i<pixelCount; ++i) {
		Float correction = luminanceFactor;
		if (importanceMap)
			correction *= importanceMap[i];
		Spectrum value = accum[i] * correction;
		if (direct)
			value += direct[i];
		target[i] = value;
	}

	m_film->setBitmap(m_developBuffer);
	m_refreshTimer->reset();

	m_queue->signalRefresh(m_job);
}

void MLTProcess::processResult(const WorkResult *wr, bool cancelled) {
	LockGuard lock(m_resultMutex);
	const ImageBlock *result = static_cast<const ImageBlock *>(wr);
	m_accum->put(result);
	m_progress->update(++m_resultCounter);
	m_refreshTimeout = std::min(2000U, m_refreshTimeout * 2);

	/* Re-develop the entire image every two seconds if partial results are
	   visible (e.g. in a graphical user interface). Do it a bit more often
	   at the beginning. */
	if (m_job->isInteractive() && m_refreshTimer->getMilliseconds() > m_refreshTimeout)
		develop();
}

ParallelProcess::EStatus MLTProcess::generateWork(WorkUnit *unit, int worker) {
	int timeout = 0;
	if (m_config.timeout > 0) {
		timeout = static_cast<int>(static_cast<int64_t>(m_config.timeout*1000) -
		          static_cast<int64_t>(m_timeoutTimer->getMilliseconds()));
	}

	if (m_workCounter >= m_config.workUnits || timeout < 0)
		return EFailure;

	SeedWorkUnit *workUnit = static_cast<SeedWorkUnit *>(unit);
	workUnit->setSeed(m_seeds[m_workCounter++]);
	workUnit->setTimeout(timeout);
	return ESuccess;
}

void MLTProcess::bindResource(const std::string &name, int id) {
	ParallelProcess::bindResource(name, id);
	if (name == "sensor") {
		m_film = static_cast<Sensor *>(Scheduler::getInstance()->getResource(id))->getFilm();
		if (m_progress)
			delete m_progress;
		m_progress = new ProgressReporter("Rendering", m_config.workUnits, m_job);
		m_accum = new ImageBlock(Bitmap::ESpectrum, m_film->getCropSize());
		m_accum->clear();
		m_developBuffer = new Bitmap(Bitmap::ESpectrum, Bitmap::EFloat, m_film->getCropSize());
	}
}

ref<ResponsiveIntegrator> MLTProcess::makeResponsiveIntegrator(Integrator* mlt, const MLTConfiguration *config) {
	return new MLTRenderer::MLTResponsive(mlt, config);
}

MTS_IMPLEMENT_CLASS_S(MLTRenderer, false, WorkProcessor)
MTS_IMPLEMENT_CLASS(MLTProcess, false, ParallelProcess)
MTS_IMPLEMENT_CLASS(SeedWorkUnit, false, WorkUnit)

MTS_NAMESPACE_END
