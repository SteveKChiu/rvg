#pragma once

#include "fwd.hpp"
#include "widget.hpp"
#include "style.hpp"
#include <functional>

namespace vui {

class ColorPicker : public Widget {
public:
	std::function<void(ColorPicker&)> onChange;

public:
	ColorPicker(Gui&, const Rect2f& bounds, const Color& start = {20, 20, 20});
	ColorPicker(Gui&, const Rect2f& bounds, const Color& start,
			const ColorPickerStyle& style);

	void size(Vec2f) override;
	using Widget::size;

	void hide(bool hide) override;
	bool hidden() const override;

	Widget* mouseButton(const MouseButtonEvent&) override;
	Widget* mouseMove(const MouseMoveEvent&) override;
	void draw(const DrawInstance&) const override;

	void pick(const vgv::Color&);
	Color picked() const;

	float currentHue() const;
	Vec2f currentSV() const;

	const auto& style() const { return style_.get(); }

protected:
	void click(Vec2f pos);
	void size(Vec3f hsv, Vec2f size);

protected:
	std::reference_wrapper<const ColorPickerStyle> style_;

	Shape hue_;
	RectShape hueMarker_;

	RectShape selector_;
	CircleShape colorMarker_;

	Paint basePaint_ {};
	Paint sGrad_ {}; // saturation gradient
	Paint vGrad_ {}; // value gradient

	bool sliding_ {};
};

} // namespace vui