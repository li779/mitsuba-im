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
#if !defined(__MITSUBA_RENDER_SCENEHANDLER_H_)
#define __MITSUBA_RENDER_SCENEHANDLER_H_

#ifndef MTS_USE_PUGIXML
#include <xercesc/sax/HandlerBase.hpp>
#include <xercesc/sax/AttributeList.hpp>
#else
#include <pugixml.hpp>
#endif
#include <mitsuba/core/plugin.h>
#include <mitsuba/core/properties.h>
#include <mitsuba/core/version.h>
#include <unordered_map>
#include <stack>
#include <map>
#include <functional>

#ifndef MTS_USE_PUGIXML
XERCES_CPP_NAMESPACE_BEGIN
class SAXParser;
class XMLTranscoder;
XERCES_CPP_NAMESPACE_END
#endif

MTS_NAMESPACE_BEGIN
#ifndef MTS_USE_PUGIXML
namespace xercesc = XERCES_CPP_NAMESPACE;
#endif

/// Push a cleanup handler to be executed after loading the scene is done
extern MTS_EXPORT_RENDER void pushSceneCleanupHandler(void (*cleanup)());

/**
 * \brief XML parser for Mitsuba scene files. To be used with the
 * SAX interface of Xerces-C++.
 *
 * \remark In the Python bindings, only the static function
 *         \ref loadScene() is exposed.
 * \ingroup librender
 * \ingroup libpython
 */
class MTS_EXPORT_RENDER SceneHandler
#ifndef MTS_USE_PUGIXML
	: public xercesc::HandlerBase {
	typedef xercesc::AttributeList AttributeList;
#else
	: public pugi::xml_tree_walker {
	typedef pugi::xml_attribute AttributeList;
	typedef pugi::char_t XMLCh;
	typedef size_t XMLSize_t;

	friend class SceneLoader;
#endif
public:
	typedef std::map<std::string, ConfigurableObject *> NamedObjectMap;
	typedef std::map<std::string, std::string, SimpleStringOrdering> ParameterMap;

	SceneHandler(const ParameterMap &params, NamedObjectMap *objects = NULL,
			bool isIncludedFile = false);
	virtual ~SceneHandler();

	/// Convenience method -- load a scene from a given filename
	static ref<Scene> loadScene(const fs::pathstr &filename,
		const ParameterMap &params= ParameterMap());

	/// Convenience method -- load a scene from a given string
	static ref<Scene> loadSceneFromString(const std::string &string,
		const ParameterMap &params= ParameterMap());

	/// Initialize Xerces-C++ (needs to be called once at program startup)
	static void staticInitialization();

	/// Free the memory taken up by staticInitialization()
	static void staticShutdown();

	// -----------------------------------------------------------------------
	//  Implementation of the SAX DocumentHandler interface
	// -----------------------------------------------------------------------
	virtual void startDocument();
	virtual void endDocument();
	virtual void startElement(
		const XMLCh* const name,
		AttributeList& attributes
	);
	virtual void endElement(const XMLCh* const name);
	virtual void characters(const XMLCh* const chars, const XMLSize_t length);
#ifndef MTS_USE_PUGIXML
    virtual void setDocumentLocator(const Locator* const locator);
#else
	virtual bool begin(pugi::xml_node& node);
	virtual bool for_each(pugi::xml_node& node);
	virtual bool end(pugi::xml_node& node);
#endif

	inline const Scene *getScene() const { return m_scene.get(); }
	inline Scene *getScene() { return m_scene; }

#ifndef MTS_USE_PUGIXML
	// -----------------------------------------------------------------------
	//  Implementation of the SAX ErrorHandler interface
	// -----------------------------------------------------------------------
	void warning(const xercesc::SAXParseException& exc);
	void error(const xercesc::SAXParseException& exc);
	void fatalError(const xercesc::SAXParseException& exc);
#endif
protected:
	std::string transcode(const XMLCh * input) const;

	Float parseFloat(const std::string &name, const std::string &str,
			Float defVal = -1) const;

	void clear();

private:
	/**
	 * Enumeration of all possible tags that can be encountered in a
	 * Mitsuba scene file
	 */
	enum ETag {
		EScene, EShape, ESampler, EFilm,
		EIntegrator, ETexture, ESensor,
		EEmitter, ESubsurface, EMedium,
		EVolume, EPhase, EBSDF, ERFilter,
		ENull, EReference, EInteger, EFloat,
		EBoolean, EString, ETranslate, ERotate,
		ELookAt, EScale, EMatrix, EPoint,
		EVector, ERGB, ESRGB, EBlackBody,
		ESpectrum, ETransform, EAnimation,
		EInclude, EAlias, EDefault
	};

	struct ParseContext {
		inline ParseContext(ParseContext *_parent, ETag tag)
		 : parent(_parent), tag(tag) { }

		ParseContext *parent;
		ETag tag;
		Properties properties;
		std::map<std::string, std::string> attributes;
		std::vector<std::pair<std::string, ConfigurableObject *> > children;
	};


	typedef std::pair<ETag, const Class *> TagEntry;
	typedef std::unordered_map<std::string, TagEntry> TagMap;

#ifndef MTS_USE_PUGIXML
	const xercesc::Locator *m_locator;
	xercesc::XMLTranscoder* m_transcoder;
#else
	ptrdiff_t m_locator;
	std::function<std::string(ptrdiff_t)> m_locatorCtx;
#endif
	ref<Scene> m_scene;
	ParameterMap m_params;
	NamedObjectMap *m_namedObjects;
	PluginManager *m_pluginManager;
	std::stack<ParseContext> m_context;
	TagMap m_tags;
	Transform m_transform;
	ref<AnimatedTransform> m_animatedTransform;
	bool m_isIncludedFile;
};

MTS_NAMESPACE_END

#endif /* __MITSUBA_RENDER_SCENEHANDLER_H_ */
