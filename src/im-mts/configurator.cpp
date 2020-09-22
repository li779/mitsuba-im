#include "shell.h"
#include <mitsuba/render/scene.h>
#include <mitsuba/render/integrator.h>
#include <mitsuba/render/integrator2.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

namespace impl {

	struct SceneConfigurator: ::SceneConfigurator {
		struct Configuration {
			struct Parameter {
				union {
					long long i[4];
					double f[4];
				} value;
				int components;
				ImGuiDataType type;
				std::string name;
				std::string fallbackValue;

				Parameter(std::string name_, mitsuba::Properties const& props)
					: components(0)
					, type(-1)
					, name(std::move(name_)) {
					auto mtsType = props.getType(name);
					if (mtsType == mitsuba::Properties::EBoolean) {
						type = ImGuiDataType_U8;
						value.i[0] = props.getBoolean(name);
					} else if (mtsType == mitsuba::Properties::EInteger) {
						type = ImGuiDataType_S64;
						components = 1;
						value.i[0] = props.getInteger(name);
					} else if (mtsType == mitsuba::Properties::EFloat) {
						type = ImGuiDataType_Double;
						components = 1;
						value.f[0] = props.getFloat(name);
					} else if (mtsType == mitsuba::Properties::EVector) {
						type = ImGuiDataType_Double;
						components = 3;
						auto v = props.getVector(name);
						value.f[0] = v.x;
						value.f[1] = v.y;
						value.f[2] = v.z;
					} else if (mtsType == mitsuba::Properties::EPoint) {
						type = ImGuiDataType_Double;
						components = 3;
						auto v = props.getPoint(name);
						value.f[0] = v.x;
						value.f[1] = v.y;
						value.f[2] = v.z;
					}
					else if (mtsType == mitsuba::Properties::EString) {
						type = ImGuiDataType_S8;
						fallbackValue = props.getString(name);
					}
					else {
						fallbackValue = props.getAsString(name);
					}
				}

				static ImVec2 textSize(char const* text, bool in_box = true, bool auto_x = true) {
					ImVec2 size = ImGui::CalcTextSize(text);
					if (auto_x)
						size.x = 0;
					if (in_box) {
						float fh = ImGui::GetFrameHeight();
						size.y = std::max(fh, size.y + (fh - ImGui::GetFontSize()));
					}
					return size;
				}

				bool ui() {
					// scalar
					if (components > 0) {
						return ImGui::DragScalarN(name.c_str(), type, &value, components, type == ImGuiDataType_Double ? .01f : .1f);
					}
					// string
					if (type == ImGuiDataType_S8) {
						return ImGui::InputTextMultiline(name.c_str(), &fallbackValue, textSize(fallbackValue.c_str()), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
					// bool
					} else if (type == ImGuiDataType_U8) {
						bool val = value.i[0] != 0;
						if (ImGui::Checkbox(name.c_str(), &val)) {
							value.i[0] = val;
							return true;
						}
					}
					// unknown
					else {
						ImGui::InputTextMultiline(name.c_str(), (char*) fallbackValue.c_str(), fallbackValue.size(), textSize(fallbackValue.c_str()), ImGuiInputTextFlags_ReadOnly);
					}
					// no changes
					return false;
				}

				void apply(mitsuba::Properties& props) {
					if (components > 1) {
						if (props.getType(name) == mitsuba::Properties::EVector)
							props.setVector(name, mitsuba::Vector(value.f[0], value.f[1], value.f[2]), false);
						else
							props.setPoint(name, mitsuba::Point(value.f[0], value.f[1], value.f[2]), false);
					}
					else if (components > 0) {
						if (type == ImGuiDataType_Float || type == ImGuiDataType_Double)
							props.setFloat(name, (mitsuba::Float) value.f[0], false);
						else
							props.setInteger(name, (int) value.i[0], false);
					}
					else if (type == ImGuiDataType_U8)
						props.setBoolean(name, value.i[0] != 0, false);
					else
						props.setString(name, fallbackValue, false);
				}
			};

			std::vector<Parameter> cachedParameters;

			mitsuba::Properties active;
			mitsuba::Properties defaults;
			mitsuba::Properties current;
			bool hadChanges = false;

			std::vector<std::string> cachedPlugins;
			bool refreshPlugins = false;

			std::vector<std::string> const& plugins(char const* symbol) {
				if (refreshPlugins || cachedPlugins.empty()) {
					cachedPlugins = Scene::availablePlugins(symbol, refreshPlugins);
					refreshPlugins = false;
				}
				return cachedPlugins;
			}

			void reset(mitsuba::Properties const& active) {
				this->active = active;
				set(active);
			}
			void set(mitsuba::Properties const& next) {
				mitsuba::Properties defaults;
				defaults.recordQueriesAndDefaults(true);
				defaults.setPluginName(next.getPluginName());
				bool haveDefaults = false;
				try {
					Scene::createTemplate(defaults);
					haveDefaults = true;
				} catch (...) {
					SLog(mitsuba::EWarn, "Could not record defaults for \"%s\"", defaults.getPluginName().c_str());
				}

				mitsuba::Properties all = defaults;
				all.setID(next.getID());
				all.recordQueriesAndDefaults(false);
				all.merge(next, nullptr, haveDefaults);

				this->current = all;
				this->defaults = defaults;

				refreshParameters();
			}

			void refreshParameters() {
				std::vector<std::string> paramNames;
				defaults.putPropertyNames(paramNames);
				if (paramNames.empty())
					current.putPropertyNames(paramNames);

				this->cachedParameters.clear();
				this->cachedParameters.reserve(paramNames.size());
				for (auto& n : paramNames)
					this->cachedParameters.emplace_back(n, current);
			}

			void changePlugin(char const* plugin) {
				// property types might be incorrect in this configuration,
				// this is fixed by the recording of defaults in set()
				mitsuba::Properties next = this->active;
				next.setPluginName(plugin);
				next.merge(this->current, &this->defaults);
				set(next);
				hadChanges = true;
			}

			mitsuba::Properties createParameters() const {
				mitsuba::Properties params;
				params.setPluginName(current.getPluginName());
				params.setID(current.getID());
				params.merge(current, &defaults);
				return params;
			}

			bool ui() {
				bool changes = false;
				for (auto& p : cachedParameters)
					if (p.ui()) {
						p.apply(current);
						changes = true;
					}
				hadChanges |= changes;
				return changes;
			}
		};
		Configuration integrator, film, sensor;

		SceneConfigurator(mitsuba::Scene* scene) {
			this->scene = scene;

			if (auto currentIntegrator = scene->getIntegrator())
				this->integrator.reset(currentIntegrator->getProperties());
			if (auto currentSensor = scene->getSensor()) {
				this->sensor.reset(currentSensor->getProperties());
				if (auto currentFilm = currentSensor->getFilm())
					this->film.reset(currentFilm->getProperties());
			}
		}

		bool run() override {
			if (!ImGui::BeginTabBar("Configure"))
				return false;

			bool haveChanges = false;
			bool applyChanges = this->wantAutoApply;
			bool* tabChanges = nullptr;

			auto makeTab = [&](Configuration& component, char const* plugin_symbol) {
				if (ImGui::BeginCombo("Type", component.current.getPluginName().c_str())) {
					auto& plugins = component.plugins(plugin_symbol);
					std::string const* selectedPlugin = nullptr;
					for (auto& p : plugins) {
						if (ImGui::Selectable(p.c_str()))
							selectedPlugin = &p;
					}

					if (ImGui::Selectable("<refresh list>"))
						component.refreshPlugins = true;
					ImGui::EndCombo();

					if (selectedPlugin)
						component.changePlugin(selectedPlugin->c_str());
				}

				component.ui();

				tabChanges = &component.hadChanges;
			};

			integrator.hadChanges = false;
			if (ImGui::BeginTabItem("Integrator")) {
				makeTab(integrator, "mitsuba_integrator_plugin");
				ImGui::EndTabItem();
			}
			haveChanges |= integrator.hadChanges;

			film.hadChanges = false;
			if (ImGui::BeginTabItem("Film")) {
				makeTab(film, "mitsuba_film_plugin");

				if (ImGui::Button("Scale to Canvas")) {
					auto& io = ImGui::GetIO();
					film.current.setInteger("width", io.DisplaySize.x);
					film.current.setInteger("height", io.DisplaySize.y);
					film.refreshParameters();
					film.hadChanges = true;
				}
				
				ImGui::EndTabItem();
			}
			haveChanges |= film.hadChanges;

			sensor.hadChanges = false;
			if (ImGui::BeginTabItem("Sensor")) {
				makeTab(sensor, "mitsuba_sensor_plugin");
				ImGui::EndTabItem();
			}
			haveChanges |= sensor.hadChanges;

			ImGui::EndTabBar();

			if (ImGui::Button("Apply")) {
				if (tabChanges)
					*tabChanges = true;
				haveChanges = true;
				applyChanges = true;
			}
			ImGui::SameLine();
			ImGui::Checkbox("Auto", &wantAutoApply);

			return haveChanges && applyChanges;
		}

		struct Changes : ::SceneConfigurator::Changes {
			mitsuba::Properties integrator, film, sensor;

			Changes(SceneConfigurator const* configurator) {
				if (configurator->integrator.hadChanges)
					integrator = configurator->integrator.createParameters();
				if (configurator->film.hadChanges)
					film = configurator->film.createParameters();
				if (configurator->sensor.hadChanges)
					sensor = configurator->sensor.createParameters();
			}

			void apply(mitsuba::Scene* scene) override {
				if (!integrator.getPluginName().empty()) {
					try {
						mitsuba::ref<mitsuba::ConfigurableObject> newIntegrator
							= Scene::createTemplate(integrator, MTS_CLASS(mitsuba::Integrator));
						newIntegrator->configure();
						scene->setIntegrator(static_cast<mitsuba::Integrator*>(newIntegrator.get()));
					}
					catch (...) {
						SLog(mitsuba::EWarn, "Failed to apply integrator \"%s\"", integrator.getPluginName().c_str());
					}
				}

				mitsuba::ref<mitsuba::Film> newFilm;
				if (!film.getPluginName().empty()) {
					try {
						mitsuba::ref<mitsuba::ConfigurableObject> newFilm_
							= Scene::createTemplate(film, MTS_CLASS(mitsuba::Film));
						newFilm_->configure();
						newFilm = static_cast<mitsuba::Film*>(newFilm_.get());
					}
					catch (...) {
						SLog(mitsuba::EWarn, "Failed to apply film \"%s\"", film.getPluginName().c_str());
					}
				}

				mitsuba::ref<mitsuba::Sensor> oldSensor = scene->getSensor();
				mitsuba::ref<mitsuba::Sensor> newSensor;
				if (!sensor.getPluginName().empty()) {
					try {
						if (oldSensor) {
							newSensor = Scene::createModifiedSensor(sensor, *oldSensor, nullptr, newFilm);
						} else {
							mitsuba::ref<mitsuba::ConfigurableObject> newSensor_
								= Scene::createTemplate(sensor, MTS_CLASS(mitsuba::Sensor));
							newSensor = static_cast<mitsuba::Sensor*>(newSensor_.get());
							if (newFilm)
								newSensor->addChild(newFilm);
							newSensor->addChild(scene->getSampler());
							newSensor->configure();
						}
					}
					catch (...) {
						SLog(mitsuba::EWarn, "Failed to apply sensor \"%s\"", sensor.getPluginName().c_str());
					}
				}
				else if (newFilm && oldSensor) {
					try { newSensor = Scene::cloneSensor(*oldSensor, nullptr, newFilm); }
					catch (...) { }
				}

				if (newSensor) {
					scene->addSensor(newSensor);
					scene->setSensor(newSensor);
					if (oldSensor)
						scene->removeSensor(oldSensor);
				} else if (newFilm) {
					SLog(mitsuba::EWarn, "Failed to apply film to sensor");
				}
			}
		};
		Changes* changes() const override {
			return new Changes(this);
		}
	};

} // namespace

SceneConfigurator* SceneConfigurator::create(mitsuba::Scene* scene) {
	return new impl::SceneConfigurator(scene);
}
SceneConfigurator::~SceneConfigurator() = default;
SceneConfigurator::Changes::~Changes() = default;

