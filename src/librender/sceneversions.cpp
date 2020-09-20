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

#ifdef MTS_USE_PUGIXML

#include <mitsuba/core/platform.h>

#include <pugixml.hpp>
#include <string>

MTS_NAMESPACE_BEGIN

using namespace pugi;

static pugi::xml_attribute an_attribute(pugi::xml_node n, pugi::char_t const* name) {
	pugi::xml_attribute a = n.attribute(name);
	if (!a) a = n.append_attribute(name);
	return a;
}

void upgrade_to_030(pugi::xml_node& root) {
	auto scene = root.select_node("descendant-or-self::scene").node();
	if (!scene)
		return;
	an_attribute(scene, "version").set_value("0.3.0");

	for (auto r : scene.select_nodes("//lookAt[@ox]")) {
		auto n = r.node();
		n.parent().insert_child_before("scale", n).append_attribute("x").set_value("-1");
		pugi::xml_attribute o[3] = { n.attribute("ox"), n.attribute("oy"), n.attribute("oz") };
		pugi::xml_attribute t[3] = { n.attribute("tx"), n.attribute("ty"), n.attribute("tz") };
		pugi::xml_attribute u[3] = { n.attribute("ux"), n.attribute("uy"), n.attribute("uz") };
		n.append_attribute("origin").set_value( ((pugi::string_t) o[0].value() + ", " + o[1].value() + ", " + o[2].value()).c_str() );
		n.append_attribute("target").set_value( ((pugi::string_t) t[0].value() + ", " + t[1].value() + ", " + t[2].value()).c_str() );
		if (u[0])
			n.append_attribute("up").set_value( ((pugi::string_t) u[0].value() + ", " + u[1].value() + ", " + u[2].value()).c_str() );
		for (int i = 0; i < 3; ++i) {
			n.remove_attribute(o[i]);
			n.remove_attribute(t[i]);
			n.remove_attribute(u[i]);
		}
	}

	for (auto r : scene.select_nodes("//shape[@type='obj']")) {
		auto n = r.node();
		auto b = n.append_child("boolean");
		b.append_attribute("name").set_value("flipTexCoords");
		b.append_attribute("value").set_value("false");
	}

	for (auto r : scene.select_nodes("//shape[not(bsdf) and not(ref) and not(medium) and not(subsurface)]")) {
		auto n = r.node();
		n.append_child("bsdf").append_attribute("type").set_value("diffuse");
	}

	for (auto r : scene.select_nodes("//bsdf[@type='microfacet' or @type='phong' or @type='ward']")) {
		auto n = r.node();
		pugi::xml_node ans[] = {
			  n.select_node("float[@name='diffuseAmount']").node()
			, n.select_node("float[@name='specularAmount']").node()
		};
		pugi::string_t avs[] = {
			  ans[0] ? ans[0].attribute("value").value() : "1.0" // diffuse
			, ans[1] ? ans[1].attribute("value").value() : "1.0" // specular
		};
		pugi::char_t const* rvs[] = {
			  "0.0" // diffuse
			, "1.0" // specular
		};
		pugi::string_t bsdfType = n.attribute("type").value();
		if (bsdfType == "phong" || bsdfType == "ward") {
			rvs[0] = "0.5";
			rvs[1] = "0.2";
		}
		pugi::char_t const* rnns[] = {
			  "diffuseReflectance"
			, "specularReflectance"
		};
		pugi::xml_node rns[] = {
			  n.select_node("node()[@name='diffuseReflectance']").node()
			, n.select_node("node()[@name='specularReflectance']").node()
		};
		for (int i = 0; i < 2; ++i) {
			if (rns[i]) {
				if (pugi::xpath_query( ("number(" + avs[i] + ")!=1").c_str() ).evaluate_boolean(rns[i])) {
					auto stn = n.append_child("texture");
					stn.append_attribute("name").set_value(rnns[i]);
					stn.append_attribute("type").set_value("scale");
					auto sn = stn.append_child("float");
					sn.append_attribute("name").set_value("scale");
					sn.append_attribute("value").set_value(avs[i].c_str());
					an_attribute(stn.append_move(rns[i]), "name").set_value("value");
				}
			} else {
				rns[i] = n.append_child("spectrum");
				rns[i].append_attribute("name").set_value(rnns[i]);
				rns[i].append_attribute("value").set_value( pugi::xpath_query( ("number(" + avs[i] + ")*number(" + rvs[i] + ")").c_str() ).evaluate_string(rns[i]).c_str() );
			}
			if (ans[i])
				n.remove_child(ans[i]);
		}
	}

	for (auto r : scene.select_nodes("//shape[@type='sphere']/boolean[@name='inverted']/@name")) {
		r.attribute().set_value("flipNormals");
	}
	for (auto r : scene.select_nodes("//shape[@type='cylinder']/point[@name='p1']/@name")) {
		r.attribute().set_value("p0");
	}
	for (auto r : scene.select_nodes("//shape[@type='cylinder']/point[@name='p2']/@name")) {
		r.attribute().set_value("p1");
	}

	for (auto r : scene.select_nodes("//texture[@type='checkerboard']/spectrum[@name='brightColor']/@name")) {
		r.attribute().set_value("color0");
	}
	for (auto r : scene.select_nodes("//texture[@type='checkerboard']/spectrum[@name='darkColor']/@name")) {
		r.attribute().set_value("color1");
	}
	for (auto r : scene.select_nodes("//texture[@type='gridtexture']/spectrum[@name='brightColor']/@name")) {
		r.attribute().set_value("color0");
	}
	for (auto r : scene.select_nodes("//texture[@type='gridtexture']/spectrum[@name='darkColor']/@name")) {
		r.attribute().set_value("color1");
	}

	for (auto r : scene.select_nodes("//bsdf[@type='lambertian']/@type")) {
		r.attribute().set_value("diffuse");
	}
	for (auto r : scene.select_nodes("//bsdf[@type='ward']/float[@name='alphaX']/@name")) {
		r.attribute().set_value("alphaU");
	}
	for (auto r : scene.select_nodes("//bsdf[@type='ward']/float[@name='alphaY']/@name")) {
		r.attribute().set_value("alphaV");
	}
	for (auto r : scene.select_nodes("//bsdf[@type='microfacet']/float[@name='alphaB']/@name")) {
		r.attribute().set_value("alpha");
	}
	for (auto r : scene.select_nodes("//bsdf[@type='microfacet']/@type")) {
		r.attribute().set_value("roughplastic");
	}
	for (auto r : scene.select_nodes("//bsdf[@type='mirror']")) {
		auto n = r.node();
		n.attribute("type").set_value("conductor");
		auto m = n.append_child("string");
		m.append_attribute("name").set_value("material");
		m.append_attribute("value").set_value("Cr");
	}
	for (auto r : scene.select_nodes("//float[@name='sizeMultiplier']/@name")) {
		r.attribute().set_value("densityMultiplier");
	}
	for (auto r : scene.select_nodes("bsdf[@type='roughmetal']/float[@name='alphaB']/@name")) {
		r.attribute().set_value("alpha");
	}
	for (auto r : scene.select_nodes("//bsdf[@type='roughmetal']/float[@name='ior']/@name")) {
		r.attribute().set_value("eta");
	}
	for (auto r : scene.select_nodes("//bsdf[@type='roughmetal']/@type")) {
		r.attribute().set_value("roughconductor");
	}
	for (auto r : scene.select_nodes("//bsdf[@type='roughglass']/float[@name='alphaB']/@name")) {
		r.attribute().set_value("alpha");
	}
	for (auto r : scene.select_nodes("//bsdf[@type='roughglass']/@type")) {
		r.attribute().set_value("roughdielectric");
	}
	for (auto r : scene.select_nodes("//bsdf[@type='composite']/@type")) {
		r.attribute().set_value("mixturebsdf");
	}

	for (auto r : scene.select_nodes("//texture[@type='exrtexture']/@type")) {
		r.attribute().set_value("bitmap");
	}
	for (auto r : scene.select_nodes("//texture[@type='ldrtexture']/@type")) {
		r.attribute().set_value("bitmap");
	}
}

void upgrade_to_040(pugi::xml_node& root) {
	auto scene = root.select_node("descendant-or-self::scene").node();
	if (!scene)
		return;
	scene.attribute("version").set_value("0.4.0");

	for (auto r : scene.select_nodes("//camera/transform[@name='toWorld']")) {
		r.node().prepend_child("scale").append_attribute("x").set_value("-1");
	}
	for (auto r : scene.select_nodes("//camera")) {
		auto n = r.node();
		auto msn = n.select_node("boolean[@name='mapSmallerSide']").node();
		if ((pugi::string_t) n.attribute("type").value() == "perspective") {
			pugi::char_t const* fovAxis = (msn && (pugi::string_t) msn.attribute("value").value() == "false") ? "larger" : "smaller";
			if (msn)
				msn.set_name("string");
			else
				msn = n.append_child("string");
			an_attribute(n, "name").set_value("fovAxis");
			an_attribute(n, "value").set_value(fovAxis);	
		}
		else
			n.remove_child(msn);
	}

	for (auto r : scene.select_nodes("//luminaire[@type='area' or @type='constant']/node()[@name='intensity']/@name")) {
		r.attribute().set_value("radiance");
	}
	for (auto r : scene.select_nodes("//luminaire[@type='directional']/node()[@name='intensity']/@name")) {
		r.attribute().set_value("irradiance");
	}

	for (auto r : scene.select_nodes("//sampler/node()[@name='depth']/@name")) {
		r.attribute().set_value("dimension");
	}

	for (auto r : scene.select_nodes("//integrator/node()[@name='luminaireSamples']/@name")) {
		r.attribute().set_value("emitterSamples");
	}
	for (auto r : scene.select_nodes("//integrator[@type='errctrl']/@type")) {
		r.attribute().set_value("adaptive");
	}

	for (auto r : scene.select_nodes("//film/boolean[@name='alpha']")) {
		auto n = r.node();
		n.set_name("string");
		n.attribute("name").set_value("pixelFormat");
		auto va = n.attribute("value");
		if ((pugi::string_t) va.value() == "true")
			va.set_value("rgba");
		else
			va.set_value("rgb");
	}
	for (auto r : scene.select_nodes("//film[@type='exrfilm']/@type")) {
		r.attribute().set_value("hdrfilm");
	}
	for (auto r : scene.select_nodes("//film[@type='pngfilm']/@type")) {
		r.attribute().set_value("ldrfilm");
	}

	for (auto r : scene.select_nodes("//float[@name='focusDepth']/@name")) {
		r.attribute().set_value("focusDistance");
	}
	for (auto r : scene.select_nodes("//float[@name='intensityScale']/@name")) {
		r.attribute().set_value("scale");
	}
	for (auto r : scene.select_nodes("//float[@name='densityMultiplier']/@name")) {
		r.attribute().set_value("scale");
	}
	for (auto r : scene.select_nodes("//blackbody/@multiplier")) {
		r.attribute().set_name("scale");
	}

	for (auto r : scene.select_nodes("//camera")) {
		r.node().set_name("sensor");
	}
	for (auto r : scene.select_nodes("//luminaire")) {
		r.node().set_name("emitter");
	}
}

void upgrade_to_050(pugi::xml_node& root) {
	auto scene = root.select_node("descendant-or-self::scene").node();
	if (!scene)
		return;
	scene.attribute("version").set_value("0.5.0");

	for (auto r : scene.select_nodes("//bsdf[@type='bump']/@type")) {
		r.attribute().set_value("bumpmap");
	}
}

void upgrade_to_060(pugi::xml_node& root) {
	auto scene = root.select_node("descendant-or-self::scene").node();
	if (!scene)
		return;
	scene.attribute("version").set_value("0.6.0");
}

MTS_NAMESPACE_END

#endif