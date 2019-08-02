/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media_unwrapped.h"
#include "ui/text/text_isolated_emoji.h"

namespace Data {
struct FileOrigin;
} // namespace Data

namespace Lottie {
class SinglePlayer;
} // namespace Lottie

namespace HistoryView {

class LargeEmoji final : public UnwrappedMedia::Content {
public:
	LargeEmoji(
		not_null<Element*> parent,
		Ui::Text::IsolatedEmoji emoji);

	QSize size() override;
	void draw(Painter &p, const QRect &r, bool selected) override;

private:
	const not_null<Element*> _parent;
	const Ui::Text::IsolatedEmoji _emoji;
	std::shared_ptr<Image> _image;
	QSize _size;

};

} // namespace HistoryView
