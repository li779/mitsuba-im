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

#pragma once
#if !defined(__MITSUBA_RENDER_INTEGRATOR2_H_)
#define __MITSUBA_RENDER_INTEGRATOR2_H_

#include <mitsuba/core/object.h>
#include <mitsuba/core/properties.h>
#include <mitsuba/render/shape.h>

MTS_NAMESPACE_BEGIN

/**
 * \brief Abstract integrator base-class; does not make any assumptions on
 * how radiance is computed.
 *
 * In Mitsuba, the different rendering techniques are collectively referred to as
 * \a integrators, since they perform integration over a high-dimensional
 * space. Each integrator represents a specific approach for solving
 * the light transport equation---usually favored in certain scenarios, but
 * at the same time affected by its own set of intrinsic limitations.
 * Therefore, it is important to carefully select an integrator based on
 * user-specified accuracy requirements and properties of the scene to be
 * rendered.
 *
 * This is the base class of all integrators; it does not make any assumptions on
 * how radiance is computed, which allows for many different kinds of implementations
 * ranging from software-based path tracing and Markov-Chain based techniques such
 * as Metropolis Light Transport up to hardware-accelerated rasterization.
 *
 * \ingroup librender
 */
class MTS_EXPORT_RENDER ResponsiveIntegrator : public ConfigurableObject {
public:
	class Interrupt;
	struct Controls {
		int volatile const* continu;
		int volatile const* abort;
		Interrupt* interrupt;
	};
	class Interrupt {
	public:
		virtual int progress(ResponsiveIntegrator* integrator, const Scene &scene, const Sensor &sensor, Sampler &sampler, ImageBlock& target, double spp
			, Controls controls, int threadIdx, int threadCount) = 0;
	};

	/**
	 * \brief Possibly perform a pre-process task.
	 *
	 * This function is called automatically before the main rendering process;
	 * the default implementation does nothing.
	 */
	virtual bool preprocess(const Scene *scene, const Sensor* sensor, const Sampler* sampler);

	/**
	 * \brief This function is called automatically before the first rendering process;
	 * the default implementation does nothing.
	 */
	virtual bool allocate(const Scene &scene, Sampler *const *samplers, ImageBlock *const *targets, int threadCount);

	/**
	 * \brief Render the scene as seen by the given sensor (or default sensor, for some path-space algorithms).
	 */
	virtual int render(const Scene &scene, const Sensor &sensor, Sampler &sampler, ImageBlock& target
		, Controls controls, int threadIdx, int threadCount) = 0;
	
	/**
	 * \brief Lower bound for the amount of undersampling within one pixel (1 is default, as for independent samplers).
	 * Correlated samplers might set this to 0 when they generate meaningful information at lower rates than 1 spp.
	 */
	virtual Float getLowerSampleBound() const;

	/**
	* \brief Real-time statistics, nullptr by default.
	*/
	virtual char const* getRealtimeStatistics();

	MTS_DECLARE_CLASS()

	/// Create a integrator
	ResponsiveIntegrator(const Properties &props);
	/// Virtual destructor
	virtual ~ResponsiveIntegrator();
};

/** \brief Abstract base class, which describes integrators scheduled per pixel.
 * \ingroup librender
 */
class MTS_EXPORT_RENDER ImageOrderIntegrator : public ResponsiveIntegrator {
public:
	/**
	 * \brief Render the scene as seen by the given sensor (or default sensor, for some path-space algorithms).
	 */
	virtual int render(const Scene &scene, const Sensor &sensor, Sampler &sampler
		, ImageBlock& target, Point2i pixel, int threadIdx, int threadCount, void* userData) = 0;

	// prepare px permutation
	bool allocate(const Scene &scene, Sampler *const *samplers, ImageBlock *const *targets, int threadCount) override;
	// redirect through px permutation
	int render(const Scene &scene, const Sensor &sensor, Sampler &sampler, ImageBlock& target
		, Controls controls, int threadIdx, int threadCount) override;

	MTS_DECLARE_CLASS()

	/// Create a integrator
	ImageOrderIntegrator(const Properties &props);
	/// Virtual destructor
	virtual ~ImageOrderIntegrator();

protected:
	/**
	 * \brief Actual render loop, for derived classes to call with additional data.
	 */
	int render(const Scene &scene, const Sensor &sensor, Sampler &sampler, ImageBlock& target
		, Controls controls, int threadIdx, int threadCount, void* userData);

	std::vector<int> m_pxPermutation;
};

struct PixelSample {
		RayDifferential ray;
		Point2 point;
		Float time;
};
struct PixelDifferential {
	Float scale;
	MTS_EXPORT_RENDER PixelDifferential(int sampleCount);
	MTS_EXPORT_RENDER Spectrum sample(PixelSample& sample, Sensor const& sensor, Point2i px, Sampler& sampler);
};

/*
 * \brief Wrapper class of all recursive Monte Carlo integrators implemented in classic
 * mitsuba, which compute unbiased solutions to the rendering equation (and optionally
 * the radiative transfer equation).
 * \ingroup librender
 */
class MTS_EXPORT_RENDER ClassicSamplingIntegrator : public ImageOrderIntegrator {
public:
	struct MTS_EXPORT_RENDER SchedulerResourceContext {
		int sceneID;
		int sensorID;
		int samplerID;
		ref<Scheduler> scheduler;

		SchedulerResourceContext(const Scene *scene, const Sensor* sensor, const Sampler* sampler);
		~SchedulerResourceContext();
	};

	bool allocate(const Scene &scene, Sampler *const *samplers, ImageBlock *const *targets, int threadCount) override;

	bool preprocess(const Scene *scene, const Sensor* sensor, const Sampler* sampler) override;
	using ImageOrderIntegrator::render;
	int render(const Scene &scene, const Sensor &sensor, Sampler &sampler
		, ImageBlock& target, Point2i pixel, int threadIdx, int threadCount, void* userData) override;
	// utility function for derived classes using mutable classic integrators
	int render(SamplingIntegrator& threadLocalIntegrator, const Scene &scene, const Sensor &sensor, Sampler &sampler
		, ImageBlock& target, Point2i pixel, int threadIdx, int threadCount);

	MTS_DECLARE_CLASS()

	/// Create a integrator
	ClassicSamplingIntegrator(SamplingIntegrator* classic, const Properties &props);
	/// Virtual destructor
	virtual ~ClassicSamplingIntegrator();

    ref<SamplingIntegrator> classicIntegrator;
	PixelDifferential pixelDifferential;
};

MTS_NAMESPACE_END

#endif /* __MITSUBA_RENDER_INTEGRATOR_H_ */
