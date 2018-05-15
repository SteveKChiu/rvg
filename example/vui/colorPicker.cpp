#include "colorPicker.hpp"
#include "gui.hpp"
#include "window.hpp"

#include <rvg/context.hpp>
#include <dlg/dlg.hpp>
#include <nytl/rectOps.hpp>

namespace vui {
namespace {

template<size_t D, typename T>
Vec<D, T> clamp(Vec<D, T> v, const Vec<D, T>& min, const Vec<D, T>& max) {
	for(auto i = 0u; i < D; ++i) {
		v[i] = std::clamp(v[i], min[i], max[i]);
	}

	return v;
}

template<size_t D, typename T>
Vec<D, T> clamp(const Vec<D, T>& v, Rect<D, T> bounds) {
	return clamp(v, bounds.position, bounds.position + bounds.size);
}

} // anon namespace

ColorPicker::ColorPicker(Gui& gui, const Rect2f& bounds, const Color& start)
	: ColorPicker(gui, bounds, start, gui.styles().colorPicker) {
}

ColorPicker::ColorPicker(Gui& gui, const Rect2f& bounds, const Color& start,
		const ColorPickerStyle& style) : Widget(gui, bounds), style_(style) {

	auto hsv = hsvNorm(start);

	selector_ = {context()};
	hueMarker_ = {context()};
	colorMarker_ = {context()};
	basePaint_ = {context(), colorPaint(hsvNorm(hsv[0], 1.f, 1.f))};

	hue_ = {context()};
	sGrad_ = {context(), {}};
	vGrad_ = {context(), {}};

	this->size(hsv, bounds.size);

	auto drawMode = DrawMode {false, style.hueWidth};
	drawMode.color.fill = false;
	drawMode.color.stroke = true;

	for(auto i = 0u; i < 7; ++i) {
		auto col = hsvNorm(i / 6.f, 1.f, 1.f);
		drawMode.color.points.push_back(col.rgba());
	}

	auto hc = hue_.change();
	hc->drawMode = std::move(drawMode);
}

void ColorPicker::size(Vec2f size) {
	auto sv = currentSV();
	this->size({currentHue(), sv.x, sv.y}, size);
}

void ColorPicker::size(Vec3f hsv, Vec2f size) {
	if(size == Vec {autoSize, autoSize}) {
		size = {230, 200};
	} else if(size.x == autoSize) {
		size.x = std::min(0.87f * size.y, 150.f);
	} else if(size.y == autoSize) {
		size.y = std::min(1.15f * size.x, 120.f);
	}

	// selector
	auto sc = selector_.change();
	sc->size = size;
	sc->size.x -= style().hueWidth + style().huePadding;
	sc->drawMode = {true, style().strokeWidth};

	// hue
	auto hc = hue_.change();
	hc->points.clear();

	auto ystep = size.y / 6.f;
	auto x = size.x - style().hueWidth / 2;
	for(auto i = 0u; i < 7; ++i) {
		hc->points.push_back({x, ystep * i});
	}

	// hue marker
	auto hmc = hueMarker_.change();
	hmc->position.x = size.x - style().hueWidth;
	hmc->position.y = hsv[0] * size.y - style().hueMarkerHeight / 2.f;
	hmc->size = {style().hueWidth, style().hueMarkerHeight};
	hmc->drawMode = {false, style().hueMarkerThickness};

	// color marker
	using namespace nytl::vec::cw::operators;
	auto cmc = colorMarker_.change();
	cmc->center = Vec {hsv[1], 1.f - hsv[2]} * selector_.size();
	cmc->radius = {style().colorMarkerRadius, style().colorMarkerRadius};
	cmc->drawMode = {false, style().colorMarkerThickness};
	cmc->pointCount = 6u;

	// gradients
	sGrad_.paint(linearGradient(
		selector_.position(), Vec {selector_.size().x, 0.f},
		::rvg::hsv(0, 0, 255), ::rvg::hsv(0, 0, 255, 0)));
	vGrad_.paint(linearGradient(
		selector_.position(), Vec {0.f, size.y},
		::rvg::hsv(0, 255, 0, 0), ::rvg::hsv(0, 255, 0)));

	Widget::size(size);
}

void ColorPicker::hide(bool hide) {
	hue_.disable(hide);
	hueMarker_.disable(hide);
	selector_.disable(hide);
	colorMarker_.disable(hide);
}

bool ColorPicker::hidden() const {
	return hue_.disabled();
}

Widget* ColorPicker::mouseButton(const MouseButtonEvent& ev) {
	if(ev.button != MouseButton::left) {
		return nullptr;
	}

	if(!ev.pressed) {
		slidingSV_ = slidingHue_ = false;
		return this;
	}

	click(ev.position - position(), true);
	return this;
}

Widget* ColorPicker::mouseMove(const MouseMoveEvent& ev) {
	click(ev.position - position(), false);
	return this;
}

void ColorPicker::pick(const Color& color) {
	auto hsv = hsvNorm(color);

	auto hue = hsv[0];
	auto sv = Vec {hsv[1], hsv[2]};

	// update paints/shapes
	basePaint_.paint(colorPaint(hsvNorm(hue, 1.f, 1.f)));

	auto hmc = hueMarker_.change();
	hmc->position.y = hue * selector_.size().y - style().hueMarkerHeight / 2.f;

	using namespace nytl::vec::cw::operators;
	auto cmc = colorMarker_.change();
	cmc->center = sv * selector_.size();
	cmc->radius = {style().colorMarkerRadius, style().colorMarkerRadius};
}

void ColorPicker::draw(vk::CommandBuffer cb) const {
	Widget::bindState(cb);

	for(auto* p : {&basePaint_, &sGrad_, &vGrad_}) {
		p->bind(cb);
		selector_.fill(cb);
	}

	context().pointColorPaint().bind(cb);
	hue_.stroke(cb);

	if(style().stroke) {
		style().stroke->bind(cb);
		selector_.stroke(cb);
	}

	dlg_assert(style().marker);
	style().marker->bind(cb);
	hueMarker_.stroke(cb);
	colorMarker_.stroke(cb);
}

void ColorPicker::click(Vec2f pos, bool real) {
	pos = clamp(pos, Vec2f {}, size());
	auto hue = Rect2f {
		{selector_.size().x + style().huePadding, 0.f},
		{style().hueWidth, selector_.size().y}
	};

	if(slidingSV_ || (real && nytl::contains(selector_.bounds(), pos))) {
		slidingSV_ = true;
		pos = clamp(pos, selector_.bounds());
		colorMarker_.change()->center = pos;
		if(onChange) {
			onChange(*this);
		}
	} else if(slidingHue_ || (real && nytl::contains(hue, pos))) {
		slidingHue_ = true;
		pos = clamp(pos, hue);
		auto norm = pos.y / selector_.size().y;
		hueMarker_.change()->position.y = pos.y;
		hueMarker_.change()->position.y -= style().hueMarkerHeight / 2.f;
		basePaint_.paint(colorPaint(hsvNorm(norm, 1.f, 1.f)));
		if(onChange) {
			onChange(*this);
		}
	}
}

float ColorPicker::currentHue() const {
	auto pos = hueMarker_.position().y + style().hueMarkerHeight / 2.f;
	return pos / selector_.size().y;
}

Vec2f ColorPicker::currentSV() const {
	using namespace nytl::vec::cw::operators;
	auto sv = colorMarker_.center() / selector_.size();
	sv.y = 1 - sv.y;
	return sv;
}

Color ColorPicker::picked() const {
	auto sv = currentSV();
	return hsvNorm(currentHue(), sv.x, sv.y);
}

Rect2f ColorPicker::ownScissor() const {
	auto r = Widget::ownScissor();
	auto y = std::max(style().colorMarkerRadius,
		style().hueMarkerHeight / 2.f + style().hueMarkerThickness);

	r.position.x -= style().colorMarkerRadius;
	r.position.y -= y;
	r.size.x += style().colorMarkerRadius + 1.f;
	r.size.y += 2 * y;

	return r;
}

// ColorButton
ColorButton::ColorButton(Gui& gui, const Rect2f& bounds,
		const Vec2f& pickerSize, const Color& start) :
	ColorButton(gui, bounds, pickerSize, start, gui.styles().colorButton) {
}

ColorButton::ColorButton(Gui& gui, const Rect2f& bounds, const Vec2f& pickerSize,
	const Color& start, const ColorButtonStyle& style) :
		BasicButton(gui, bounds, style.button ? *style.button :
			gui.styles().basicButton), style_(style) {

	// TODO: currently hiding & unhiding on every focus
	// change (triggering an updateDevice)
	class PopupPane : public Pane {
	public:
		using Pane::Pane;
		void focus(bool gained) override {
			hide(!gained);
		}
	};

	// NOTE: makes this non movable!
	auto updateColorPaint = [this](const auto& cp){
		this->colorPaint_.paint(rvg::colorPaint(cp.picked()));
		if(onChange) {
			onChange(*this);
		}
	};

	color_ = {context()};

	auto cpBounds = Rect2f{{}, pickerSize};
	auto cp = std::make_unique<ColorPicker>(gui, cpBounds, start);
	cp_ = cp.get();
	cp_->onChange = updateColorPaint;
	colorPaint_ = {context(), rvg::colorPaint(cp_->picked())};

	pane_ = &gui.create<PopupPane>(Rect2f{{}, {autoSize, autoSize}},
		std::move(cp));
	pane_->hide(true);

	this->size(bounds.size);
}

void ColorButton::size(Vec2f size) {
	if(size.x == autoSize && size.y == autoSize) {
		size = {100.f, 25.f};
		size += 2 * style().padding;
	} else if(size.x == autoSize) {
		size.x = size.y * 4;
	} else if(size.y == autoSize) {
		size.y = size.x / 4;
	}

	auto cc = color_.change();
	cc->position = style().padding;
	cc->size = size - 2 * style().padding;
	cc->drawMode.fill = true;

	pane_->position(position() + Vec2f{0.f, size.y});
	BasicButton::size(size);
}

void ColorButton::position(Vec2f pos) {
	Widget::position(pos);
	pane_->position(position() + Vec2f{0.f, size().y});
}

void ColorButton::hide(bool hide) {
	if(hide) {
		pane_->hide(hide);
	}

	color_.disable(hide);
	BasicButton::hide(hide);
}

void ColorButton::clicked(const MouseButtonEvent&) {
	pane_->hide(false);
}

void ColorButton::focus(bool gained) {
	BasicButton::focus(gained);
	if(!gained) {
		pane_->hide(true);
	}
}

void ColorButton::draw(vk::CommandBuffer cb) const {
	BasicButton::draw(cb);
	colorPaint_.bind(cb);
	color_.fill(cb);
}

} // namespace vui
