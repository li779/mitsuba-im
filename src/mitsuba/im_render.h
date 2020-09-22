#include <mitsuba/mitsuba.h>

struct ProcessConfig {
	int concurrentAtomic = 32;
	int maxThreads = -1;

	static int recommendedThreads();
	static ProcessConfig resolveDefaults(ProcessConfig const& cfg);
};

struct InteractiveSceneProcess {
	mitsuba::ref<mitsuba::Scene> scene;
	mitsuba::ref<mitsuba::ResponsiveIntegrator> integrator;
	mitsuba::Vector2i resolution;
	int maxThreads;
	int uniqueTargets;

	float volatile *volatile *imageData;
	int numActiveThreads;

	int timeout = -1;
	int flushTimer = -1;
	int writeProgression = false;

	static InteractiveSceneProcess* create(mitsuba::Scene* scene, mitsuba::Sampler* sampler, mitsuba::ResponsiveIntegrator* integrator, ProcessConfig const& config);
	static InteractiveSceneProcess* create(mitsuba::Scene* scene, mitsuba::Sampler* sampler, mitsuba::Integrator* integrator, ProcessConfig const& config);
	virtual ~InteractiveSceneProcess();

	struct Controls {
		int volatile const* continu;
		int volatile const* abort;
	};
	virtual void render(mitsuba::Sensor* sensor, double volatile imageSamples[], Controls controls, int maxThreads = -1) = 0;
	virtual void render(int maxThreads = -1) = 0;
};
