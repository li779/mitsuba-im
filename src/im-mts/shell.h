#include <mitsuba/mitsuba.h>

int mitsuba_start(int argc, char *argv[]);
void mitsuba_shutdown();

struct ProcessConfig {
	int concurrentAtomic = 32;
	int maxThreads = -1;
	int doubleBuffered = 1;

	static int recommendedThreads();
	static ProcessConfig resolveDefaults(ProcessConfig const& cfg);
};

struct Scene {
	mitsuba::ref<mitsuba::Scene> scene;

	static Scene* create(mitsuba::Scene* scene);
	static Scene* load(fs::pathstr const& path);
	virtual ~Scene();

	static mitsuba::ref<mitsuba::Sensor> cloneSensor(mitsuba::Sensor& sensor, mitsuba::Sampler* sampler = nullptr, mitsuba::Film* film = nullptr);
	static mitsuba::ref<mitsuba::Integrator> cloneIntegrator(mitsuba::Integrator const& sensor);
	static mitsuba::ref<mitsuba::Sampler> cloneSampler(mitsuba::Sampler const& sampler);
	static mitsuba::ref<mitsuba::Scene> clonePreprocessed(mitsuba::Scene& scene);

	static std::vector<std::string> availablePlugins(char const* symbol, bool refresh);
	static mitsuba::ref<mitsuba::ConfigurableObject> createTemplate(mitsuba::Properties const& properties, mitsuba::Class const* type = nullptr);
	static mitsuba::ref<mitsuba::Sensor> createModifiedSensor(mitsuba::Properties const& properties, mitsuba::Sensor& sensor, mitsuba::Sampler* sampler = nullptr, mitsuba::Film* film = nullptr);

	static void printStats();
};

struct SceneConfigurator {
	mitsuba::ref<mitsuba::Scene> scene;
	bool wantAutoApply = true;

	static SceneConfigurator* create(mitsuba::Scene* scene);
	virtual ~SceneConfigurator();

	virtual bool run() = 0;

	struct Changes {
		virtual ~Changes();
		virtual void apply(mitsuba::Scene* scene) = 0;
	};
	virtual Changes* changes() const = 0;
};

struct SceneProcess {
	mitsuba::ref<mitsuba::Scene> scene;
	mitsuba::Vector2i resolution;

	float volatile* imageData;
	int numActiveThreads;

	static SceneProcess* create(mitsuba::Scene* scene);
	virtual ~SceneProcess();

	virtual void renderAsync(int volatile* filmRevision, int maxThreads = -1) = 0;
	virtual void wait() = 0;
	virtual bool running() = 0;
	virtual void pause(bool pause) = 0;
	virtual bool paused() const = 0;
	virtual void cancel() = 0;
	virtual bool cancelled() const = 0;

	void render(int volatile* filmRevision, int maxThreads = -1) {
		renderAsync(filmRevision, maxThreads);
		wait();
	}
};

struct InteractiveSceneProcess {
	mitsuba::ref<mitsuba::Scene> scene;
	mitsuba::ref<mitsuba::ResponsiveIntegrator> integrator;
	mitsuba::Vector2i resolution;
	int maxThreads;
	int uniqueTargets;

	float volatile *volatile *imageData;
	int numActiveThreads;
	int volatile paused;

	static InteractiveSceneProcess* create(mitsuba::Scene* scene, mitsuba::Sampler* sampler, mitsuba::ResponsiveIntegrator* integrator, ProcessConfig const& config);
	static InteractiveSceneProcess* create(mitsuba::Scene* scene, mitsuba::Sampler* sampler, mitsuba::Integrator* integrator, ProcessConfig const& config);
	virtual ~InteractiveSceneProcess();

	struct Controls {
		int volatile const* continu;
		int volatile const* abort;
	};
	virtual void render(mitsuba::Sensor* sensor, double volatile imageSamples[], Controls controls, int maxThreads = -1) = 0;
	virtual void pause(bool pause) = 0;
};

struct Preview {
	int resX, resY;
	// out
	intptr_t previewImg;
	float avgSamples;
};

struct ImagePreview : Preview {

	static ImagePreview* create(int resX, int resY);
	virtual ~ImagePreview();

	virtual void update(float const* data, int const volatile* dataRevision) = 0;
	virtual void reset() = 0;
};

struct StackedPreview : Preview {
	int readyMS = 16;
	int updateMS = 64;
	int maxSubresLevels = 3;
	float subresBias = 0.f;
	// out
	float minSppClamp = 1.f;
	int workersPerTarget = 1;

	static StackedPreview* create(int resX, int resY, int maxWorkers, int maxImages);
	virtual ~StackedPreview();

	virtual void update(unsigned long long timeStamp, float const* const* data, double const volatile dataSamples[], int maxN) = 0;
	virtual void nextGeneration() = 0;
	virtual void runGeneration(unsigned long long timestamp) = 0;
	virtual bool upToDate(double const volatile dataRevisions[], int maxN) const = 0;
	virtual bool ready(unsigned long long timestamp) const = 0;
};

struct WorkLane {
	struct Worker {
		virtual void work(WorkLane* lane) = 0;
		virtual void quit(WorkLane* lane) = 0;
	};
	struct Sync {
		virtual int sync() = 0;
	};

	volatile int continu = true;
	volatile bool started = false;
	volatile bool stopped = false;

	static WorkLane* create(Worker* worker);
	virtual ~WorkLane();

	virtual int synchronized(Sync& sync) const = 0;
	virtual void synchronize() const = 0;
};
