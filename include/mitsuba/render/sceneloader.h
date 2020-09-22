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
#if !defined(__MITSUBA_RENDER_SCENELOADER_H_)
#define __MITSUBA_RENDER_SCENELOADER_H_

#include <mitsuba/render/scene.h>
#include <mitsuba/core/version.h>

MTS_NAMESPACE_BEGIN

class SceneHandler;

#ifdef _MSC_VER
// Disable warning 4275: non dll-interface used as base for dll-interface class
// Can be safely ignored when deriving from a type in the Standard C++ Library
# pragma warning( push )
# pragma warning( disable : 4275 )
#endif

/**
 * \brief This exception is thrown when attempting to load an outdated file
 * \ingroup librender
 */
class MTS_EXPORT_RENDER VersionException : public std::runtime_error {
public:
	VersionException(const std::string &str, const Version &version) :
		std::runtime_error(str), m_version(version) { }

	/* For stupid and subtle reasons when compiling with GCC, it is important
	   that this class has a virtual member. This will ensure that its typeid
	   structure is in librender, which is important for throwing exceptions
	   across DLL boundaries */
	virtual ~VersionException() noexcept;

	inline const Version &getVersion() const { return m_version; }
private:
	Version m_version;
};

#ifdef _MSC_VER
# pragma warning( pop )
#endif

/**
 * \brief Loads scenes using the given parameters
 */
class MTS_EXPORT_RENDER SceneLoader {
public:
	typedef std::map<std::string, std::string, SimpleStringOrdering> ParameterMap;

	/// Builds a reusable scene loader for the given parameters and schema.
	SceneLoader(ParameterMap const& parameters, fs::pathstr const &schemaPath = fs::pathstr());
	SceneLoader(SceneLoader const&) = delete;
	~SceneLoader();

	/// Loads a scene from the given file path into this loader.
	ref<Scene> load(fs::pathstr const &file);

	/// Load a scene from an external file
	static ref<Scene> loadScene(const fs::pathstr &fname,
		const ParameterMap &params= ParameterMap());
	/// Load a scene from a string
	static ref<Scene> loadSceneFromString(const std::string &content,
		const ParameterMap &params= ParameterMap());

	/// Initialize Xerces-C++ (alis for SceneHandler methods, needs to be called once at program startup)
	static void staticInitialization();
	/// Free the memory taken up by staticInitialization(), alis for SceneHandler methods
	static void staticShutdown();

private:
	std::unique_ptr<SceneHandler> handler;
	void* parser;
};

MTS_NAMESPACE_END

#endif /* __MITSUBA_RENDER_SCENEHANDLER_H_ */
