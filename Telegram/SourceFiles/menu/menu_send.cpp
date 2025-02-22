/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_send.h"

#include "api/api_common.h"
#include "base/event_filter.h"
#include "boxes/abstract_box.h"
#include "core/shortcuts.h"
#include "history/view/history_view_schedule_box.h"
#include "lang/lang_keys.h"
#include "ui/widgets/popup_menu.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "history/history.h"
#include "history/history_unread_things.h"
#include "apiwrap.h"
#include "styles/style_menu_icons.h"

#include <QtWidgets/QApplication>

#include "fakepasscode/autodelete/autodelete_callback.h"

namespace SendMenu {

Fn<void()> DefaultSilentCallback(Fn<void(Api::SendOptions)> send) {
	return [=] { send({ .silent = true }); };
}

Fn<void()> DefaultScheduleCallback(
		not_null<Ui::RpWidget*> parent,
		Type type,
		Fn<void(Api::SendOptions)> send) {
	const auto weak = Ui::MakeWeak(parent);
	return [=] {
		Ui::show(
			HistoryView::PrepareScheduleBox(
				weak,
				type,
				[=](Api::SendOptions options) { send(options); }),
			Ui::LayerOption::KeepOther);
	};
}

FillMenuResult FillSendMenu(
		not_null<Ui::PopupMenu*> menu,
		Type type,
		Fn<void()> silent,
		Fn<void()> schedule,
		Fn<void()> autoDelete) {
	if (FakePasscode::DisableAutoDeleteInContextMenu()) {
		autoDelete = nullptr;
	}
	if (!silent && !schedule && !autoDelete) {
		return FillMenuResult::None;
	}
	const auto now = type;
	if (now == Type::Disabled
		|| (!silent && !autoDelete && now == Type::SilentOnly)) {
		return FillMenuResult::None;
	}

	if (silent && now != Type::Reminder) {
		menu->addAction(
			tr::lng_send_silent_message(tr::now),
			silent,
			&st::menuIconMute);
	}
	if (schedule && now != Type::SilentOnly) {
		menu->addAction(
			(now == Type::Reminder
				? tr::lng_reminder_message(tr::now)
				: tr::lng_schedule_message(tr::now)),
			schedule,
			&st::menuIconSchedule);
	}
	if (autoDelete) {
		menu->addAction(
			tr::lng_send_autodelete_message(tr::now),
			autoDelete,
			&st::menuIconDelete);
	}
	return FillMenuResult::Success;
}

void SetupMenuAndShortcuts(
		not_null<Ui::RpWidget*> button,
		Fn<Type()> type,
		Fn<void()> silent,
		Fn<void()> schedule,
		Fn<void()> autoDelete) {
	if (FakePasscode::DisableAutoDeleteInContextMenu()) {
		autoDelete = nullptr;
	}
	if (!silent && !schedule && !autoDelete) {
		return;
	}
	const auto menu = std::make_shared<base::unique_qptr<Ui::PopupMenu>>();
	const auto showMenu = [=] {
		*menu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		const auto result = FillSendMenu(*menu, type(), silent, schedule, autoDelete);
		const auto success = (result == FillMenuResult::Success);
		if (success) {
			(*menu)->popup(QCursor::pos());
		}
		return success;
	};
	base::install_event_filter(button, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::ContextMenu && showMenu()) {
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});

	Shortcuts::Requests(
	) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;

		const auto now = type();
		if (now == Type::Disabled
			|| (!silent && now == Type::SilentOnly)) {
			return;
		}
		(silent
			&& (now != Type::Reminder)
			&& request->check(Command::SendSilentMessage)
			&& request->handle([=] {
				silent();
				return true;
			}))
		||
		(schedule
			&& (now != Type::SilentOnly)
			&& request->check(Command::ScheduleMessage)
			&& request->handle([=] {
				schedule();
				return true;
			}))
		||
		(request->check(Command::JustSendMessage) && request->handle([=] {
			const auto post = [&](QEvent::Type type) {
				QApplication::postEvent(
					button,
					new QMouseEvent(
						type,
						QPointF(0, 0),
						Qt::LeftButton,
						Qt::LeftButton,
						Qt::NoModifier));
			};
			post(QEvent::MouseButtonPress);
			post(QEvent::MouseButtonRelease);
			return true;
		}));
	}, button->lifetime());
}

void SetupReadAllMenu(
		not_null<Ui::RpWidget*> button,
		Fn<PeerData*()> currentPeer,
		const QString &text,
		Fn<void(not_null<PeerData*>, Fn<void()>)> sendReadRequest) {
	struct State {
		base::unique_qptr<Ui::PopupMenu> menu;
		base::flat_set<not_null<PeerData*>> sentForPeers;
	};
	const auto state = std::make_shared<State>();
	const auto showMenu = [=] {
		const auto peer = currentPeer();
		if (!peer) {
			return;
		}
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		state->menu->addAction(text, [=] {
			if (!state->sentForPeers.emplace(peer).second) {
				return;
			}
			sendReadRequest(peer, [=] { state->sentForPeers.remove(peer); });
		}, &st::menuIconMarkRead);
		state->menu->popup(QCursor::pos());
	};

	base::install_event_filter(button, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::ContextMenu) {
			showMenu();
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});
}

void SetupUnreadMentionsMenu(
		not_null<Ui::RpWidget*> button,
		Fn<PeerData*()> currentPeer) {
	const auto text = tr::lng_context_mark_read_mentions_all(tr::now);
	const auto sendRequest = [=](not_null<PeerData*> peer, Fn<void()> done) {
		peer->session().api().request(MTPmessages_ReadMentions(
			peer->input
		)).done([=](const MTPmessages_AffectedHistory &result) {
			done();
			peer->session().api().applyAffectedHistory(peer, result);
			peer->owner().history(peer)->unreadMentions().clear();
		}).fail(done).send();
	};
	SetupReadAllMenu(button, currentPeer, text, sendRequest);
}

void SetupUnreadReactionsMenu(
		not_null<Ui::RpWidget*> button,
		Fn<PeerData*()> currentPeer) {
	const auto text = tr::lng_context_mark_read_reactions_all(tr::now);
	const auto sendRequest = [=](not_null<PeerData*> peer, Fn<void()> done) {
		peer->session().api().request(MTPmessages_ReadReactions(
			peer->input
		)).done([=](const MTPmessages_AffectedHistory &result) {
			done();
			peer->session().api().applyAffectedHistory(peer, result);
			peer->owner().history(peer)->unreadReactions().clear();
		}).fail(done).send();
	};
	SetupReadAllMenu(button, currentPeer, text, sendRequest);
}

Fn<void()> DefaultAutoDeleteCallback(
		not_null<Ui::RpWidget*> parent,
		Fn<void(object_ptr<Ui::BoxContent>)> show,
		Fn<void(Api::SendOptions)> send) {
	return FakePasscode::DefaultAutoDeleteCallback(parent, show, send);
}

Fn<void()> DefaultAutoDeleteCallback(
		not_null<Ui::RpWidget*> parent,
		Fn<void(Api::SendOptions)> send) {
	auto show = [] (object_ptr<Ui::BoxContent> box) { Ui::show(std::move(box), Ui::LayerOption::KeepOther); };
	return FakePasscode::DefaultAutoDeleteCallback(parent, show, send);
}

Fn<void()> NoAutoDeleteCallback() {
    return nullptr;
}

} // namespace SendMenu
