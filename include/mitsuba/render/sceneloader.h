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

MTS_NAMESPACE_BEGIN

class SceneHandler;

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
