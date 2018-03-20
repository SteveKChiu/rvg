// Copyright (c) 2017 nyorain
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt

#include "render.hpp"
#include "window.hpp"

#include <vgv/vgv.hpp>
#include <vgv/path.hpp>

#include <ny/backend.hpp>
#include <ny/appContext.hpp>
#include <ny/key.hpp>
#include <ny/mouseButton.hpp>
#include <ny/event.hpp>

#include <vpp/instance.hpp>
#include <vpp/debug.hpp>
#include <vpp/formats.hpp>

#include <nytl/vecOps.hpp>
#include <nytl/matOps.hpp>

#include <dlg/dlg.hpp>

#include <chrono>
#include <array>

using namespace nytl::vec::operators;
using namespace nytl::vec::cw::operators;

// settings
constexpr auto appName = "vgv-example";
constexpr auto engineName = "vpp,vgv";
constexpr auto useValidation = true;
constexpr auto startMsaa = vk::SampleCountBits::e1;
constexpr auto layerName = "VK_LAYER_LUNARG_standard_validation";
constexpr auto printFrames = true;
constexpr auto vsync = true;
// constexpr auto clearColor = std::array<float, 4>{{0.6f, 0.8f, 0.9f, 1.f}};
constexpr auto clearColor = std::array<float, 4>{{0.f, 0.f, 0.f, 1.f}};

// TODO: move to nytl
template<typename T>
void scale(nytl::Mat4<T>& mat, nytl::Vec3<T> fac) {
	for(auto i = 0; i < 3; ++i) {
		mat[i][i] *= fac[i];
	}
}

template<typename T>
void translate(nytl::Mat4<T>& mat, nytl::Vec3<T> move) {
	for(auto i = 0; i < 3; ++i) {
		mat[i][3] += move[i];
	}
}

int main() {
	// - initialization -
	auto& backend = ny::Backend::choose();
	if(!backend.vulkan()) {
		throw std::runtime_error("ny backend has no vulkan support!");
	}

	auto appContext = backend.createAppContext();

	// vulkan init: instance
	auto iniExtensions = appContext->vulkanExtensions();
	iniExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);

	vk::ApplicationInfo appInfo (appName, 1, engineName, 1, VK_API_VERSION_1_0);
	vk::InstanceCreateInfo instanceInfo;
	instanceInfo.pApplicationInfo = &appInfo;

	instanceInfo.enabledExtensionCount = iniExtensions.size();
	instanceInfo.ppEnabledExtensionNames = iniExtensions.data();

	if(useValidation) {
		instanceInfo.enabledLayerCount = 1;
		instanceInfo.ppEnabledLayerNames = &layerName;
	}

	vpp::Instance instance {};
	try {
		instance = {instanceInfo};
		if(!instance.vkInstance()) {
			throw std::runtime_error("vkCreateInstance returned a nullptr");
		}
	} catch(const std::exception& error) {
		dlg_error("Vulkan instance creation failed: {}", error.what());
		dlg_error("\tYour system may not support vulkan");
		dlg_error("\tThis application requires vulkan to work");
		throw;
	}

	// debug callback
	std::unique_ptr<vpp::DebugCallback> debugCallback;
	if(useValidation) {
		debugCallback = std::make_unique<vpp::DebugCallback>(instance);
	}

	// init ny window
	MainWindow window(*appContext, instance);
	auto vkSurf = window.vkSurface();

	const vpp::Queue* presentQueue {};
	auto device = vpp::Device(instance, vkSurf, presentQueue);

	auto renderInfo = RendererCreateInfo {
		device, vkSurf, window.size(), *presentQueue,
		startMsaa, vsync, clearColor
	};
	auto renderer = Renderer(renderInfo);


	// vgv
	vgv::Context ctx(device, renderer.renderPass(), 0);

	vgv::Transform transform(ctx);
	scale(transform.matrix, {2.f / window.size().x, 2.f / window.size().y, 1});
	translate(transform.matrix, {-1.f, -1.f, 0.f});
	transform.updateDevice();

	vgv::Polygon polygon(ctx, true);

	vgv::Paint paint(ctx, {0.1f, .6f, .3f, 1.f});

	auto fontHeight = 12;
	vgv::FontAtlas atlas(ctx);
	vgv::Font font(atlas, "OpenSans-Light.ttf", fontHeight);
	atlas.bake(ctx);

	auto string = "yo, whaddup";
	vgv::Text text(ctx, string, font, {0, 0});
	auto textWidth = font.width(string);

	text.updateDevice(ctx);

	// svg path
	auto svgSubpath = vgv::parseSvgSubpath({300, 200},
		"h -150 a150 150 0 1 0 150 -150 z");
	vgv::Polygon svgPolygon(ctx, false);
	auto points = vgv::bake(svgSubpath);
	svgPolygon.points().clear();
	for(auto& p : points) {
		svgPolygon.points().push_back(p);
		svgPolygon.points().push_back({}); // unused uv
	}

	svgPolygon.updateDevice(ctx, vgv::DrawMode::fill);

	renderer.onRender += [&](vk::CommandBuffer buf){
		// TODO
		vk::cmdBindDescriptorSets(buf, vk::PipelineBindPoint::graphics,
			ctx.pipeLayout(), 1, {ctx.dummyTex()}, {});

		transform.bind(ctx, buf);
		paint.bind(ctx, buf);

		polygon.stroke(ctx, buf);

		svgPolygon.fill(ctx, buf);
		text.draw(ctx, buf);
	};

	renderer.invalidate();

	// connect window & renderer
	auto run = true;
	window.onClose = [&](const auto&) { run = false; };
	window.onKey = [&](const auto& ev) {
		if(!ev.pressed) {
			return;
		}

		if(ev.keycode == ny::Keycode::escape) {
			dlg_info("Escape pressed, exiting");
			run = false;
		} else if(ev.keycode == ny::Keycode::b) {
			paint.color = {0.2, 0.2, 0.8, 1.f};
			paint.updateDevice();
		} else if(ev.keycode == ny::Keycode::g) {
			paint.color = {0.1, 0.6, 0.3, 1.f};
			paint.updateDevice();
		} else if(ev.keycode == ny::Keycode::r) {
			paint.color = {0.8, 0.2, 0.3, 1.f};
			paint.updateDevice();
		} else if(ev.keycode == ny::Keycode::d) {
			paint.color = {0.1, 0.1, 0.1, 1.f};
			paint.updateDevice();
		} else if(ev.keycode == ny::Keycode::w) {
			paint.color = {1, 1, 1, 1.f};
			paint.updateDevice();
		}
	};
	window.onResize = [&](const auto& ev) {
		renderer.resize(ev.size);

		text.pos.x = (ev.size[0] - textWidth) / 2;
		text.pos.y = ev.size[1] - fontHeight - 20;
		text.updateDevice(ctx);

		transform.matrix = nytl::identity<4, float>();
		auto s = nytl::Vec {2.f / window.size().x, 2.f / window.size().y, 1};
		scale(transform.matrix, s);
		translate(transform.matrix, {-1, -1, 0});
		transform.updateDevice();

		auto points = vgv::bake(svgSubpath);
		svgPolygon.points().clear();
		for(auto& p : points) {
			svgPolygon.points().push_back(p);
			svgPolygon.points().push_back({}); // unused uv
		}

		svgPolygon.updateDevice(ctx, vgv::DrawMode::fill);
	};

	vgv::Subpath subpath;
	bool first = true;

	window.onMouseButton = [&](const auto& ev) {
		if(!ev.pressed) {
			return;
		}

		auto p = static_cast<nytl::Vec2f>(ev.position);
		if(first) {
			first = false;
			subpath.start = p;
		} else {
			subpath.commands.push_back({p, vgv::SQBezierParams {}});
			auto points = vgv::bake(subpath);
			polygon.points().clear();
			for(auto p : points) {
				polygon.points().push_back(p); // pos
				// polygon.points().push_back({0.0, 0.0}); // (unused) uv
			}

			if(polygon.updateDevice(ctx, vgv::DrawMode::stroke)) {
				dlg_info("rerecord");
				renderer.invalidate();
			}
		}
	};

	// - main loop -
	using Clock = std::chrono::high_resolution_clock;
	using Secf = std::chrono::duration<float, std::ratio<1, 1>>;

	auto lastFrame = Clock::now();
	auto fpsCounter = 0u;
	auto secCounter = 0.f;

	while(run) {
		auto now = Clock::now();
		auto diff = now - lastFrame;
		auto deltaCount = std::chrono::duration_cast<Secf>(diff).count();
		lastFrame = now;

		if(!appContext->pollEvents()) {
			dlg_info("pollEvents returned false");
			return 0;
		}

		renderer.renderBlock();

		if(printFrames) {
			++fpsCounter;
			secCounter += deltaCount;
			if(secCounter >= 1.f) {
				dlg_info("{} fps", fpsCounter);
				secCounter = 0.f;
				fpsCounter = 0;
			}
		}
	}
}
