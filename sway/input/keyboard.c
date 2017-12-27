#include "sway/input/seat.h"
#include "sway/input/keyboard.h"
#include "sway/input/input-manager.h"
#include "log.h"

/*
 * Get keysyms and modifiers from the keyboard as xkb sees them.
 *
 * This uses the xkb keysyms translation based on pressed modifiers and clears
 * the consumed modifiers from the list of modifiers passed to keybind
 * detection.
 *
 * On US layout, pressing Alt+Shift+2 will trigger Alt+@.
 */
static size_t keyboard_keysyms_translated(struct sway_keyboard *keyboard,
		xkb_keycode_t keycode, const xkb_keysym_t **keysyms,
		uint32_t *modifiers) {
	struct wlr_input_device *device =
		keyboard->seat_device->input_device->wlr_device;
	*modifiers = wlr_keyboard_get_modifiers(device->keyboard);
	xkb_mod_mask_t consumed = xkb_state_key_get_consumed_mods2(
		device->keyboard->xkb_state, keycode, XKB_CONSUMED_MODE_XKB);
	*modifiers = *modifiers & ~consumed;

	return xkb_state_key_get_syms(device->keyboard->xkb_state,
		keycode, keysyms);
}

/*
 * Get keysyms and modifiers from the keyboard as if modifiers didn't change
 * keysyms.
 *
 * This avoids the xkb keysym translation based on modifiers considered pressed
 * in the state.
 *
 * This will trigger keybinds such as Alt+Shift+2.
 */
static size_t keyboard_keysyms_raw(struct sway_keyboard *keyboard,
		xkb_keycode_t keycode, const xkb_keysym_t **keysyms,
		uint32_t *modifiers) {
	struct wlr_input_device *device =
		keyboard->seat_device->input_device->wlr_device;
	*modifiers = wlr_keyboard_get_modifiers(device->keyboard);

	xkb_layout_index_t layout_index = xkb_state_key_get_layout(
		device->keyboard->xkb_state, keycode);
	return xkb_keymap_key_get_syms_by_level(device->keyboard->keymap,
		keycode, layout_index, 0, keysyms);
}

static ssize_t pressed_keysyms_index(xkb_keysym_t *pressed_keysyms,
		xkb_keysym_t keysym) {
	for (size_t i = 0; i < SWAY_KEYBOARD_PRESSED_KEYSYMS_CAP; ++i) {
		if (pressed_keysyms[i] == keysym) {
			return i;
		}
	}
	return -1;
}

static size_t pressed_keysyms_length(xkb_keysym_t *pressed_keysyms) {
	size_t n = 0;
	for (size_t i = 0; i < SWAY_KEYBOARD_PRESSED_KEYSYMS_CAP; ++i) {
		if (pressed_keysyms[i] != XKB_KEY_NoSymbol) {
			++n;
		}
	}
	return n;
}

static void pressed_keysyms_add(xkb_keysym_t *pressed_keysyms,
		xkb_keysym_t keysym) {
	ssize_t i = pressed_keysyms_index(pressed_keysyms, keysym);
	if (i < 0) {
		i = pressed_keysyms_index(pressed_keysyms, XKB_KEY_NoSymbol);
		if (i >= 0) {
			pressed_keysyms[i] = keysym;
		}
	}
}

static void pressed_keysyms_remove(xkb_keysym_t *pressed_keysyms,
		xkb_keysym_t keysym) {
	ssize_t i = pressed_keysyms_index(pressed_keysyms, keysym);
	if (i >= 0) {
		pressed_keysyms[i] = XKB_KEY_NoSymbol;
	}
}

static bool keysym_is_modifier(xkb_keysym_t keysym) {
	switch (keysym) {
	case XKB_KEY_Shift_L: case XKB_KEY_Shift_R:
	case XKB_KEY_Control_L: case XKB_KEY_Control_R:
	case XKB_KEY_Caps_Lock:
	case XKB_KEY_Shift_Lock:
	case XKB_KEY_Meta_L: case XKB_KEY_Meta_R:
	case XKB_KEY_Alt_L: case XKB_KEY_Alt_R:
	case XKB_KEY_Super_L: case XKB_KEY_Super_R:
	case XKB_KEY_Hyper_L: case XKB_KEY_Hyper_R:
		return true;
	default:
		return false;
	}
}

static void pressed_keysyms_update(xkb_keysym_t *pressed_keysyms,
		const xkb_keysym_t *keysyms, size_t keysyms_len,
		enum wlr_key_state state) {
	for (size_t i = 0; i < keysyms_len; ++i) {
		if (keysym_is_modifier(keysyms[i])) {
			continue;
		}
		if (state == WLR_KEY_PRESSED) {
			pressed_keysyms_add(pressed_keysyms, keysyms[i]);
		} else { // WLR_KEY_RELEASED
			pressed_keysyms_remove(pressed_keysyms, keysyms[i]);
		}
	}
}

static void handle_keyboard_key(struct wl_listener *listener, void *data) {
	struct sway_keyboard *keyboard =
		wl_container_of(listener, keyboard, keyboard_key);
	struct wlr_seat *wlr_seat = keyboard->seat_device->sway_seat->wlr_seat;
	struct wlr_input_device *wlr_device =
		keyboard->seat_device->input_device->wlr_device;
	struct wlr_event_keyboard_key *event = data;

	xkb_keycode_t keycode = event->keycode + 8;
	bool handled = false;
	uint32_t modifiers;
	const xkb_keysym_t *keysyms;
	size_t keysyms_len;

	// handle translated keysyms
	keysyms_len = keyboard_keysyms_translated(keyboard, keycode, &keysyms,
		&modifiers);
	pressed_keysyms_update(keyboard->pressed_keysyms_translated, keysyms,
		keysyms_len, event->state);
	if (event->state == WLR_KEY_PRESSED) {
		// TODO execute binding
	}

	// Handle raw keysyms
	keysyms_len = keyboard_keysyms_raw(keyboard, keycode, &keysyms, &modifiers);
	pressed_keysyms_update(keyboard->pressed_keysyms_raw, keysyms, keysyms_len,
		event->state);
	if (event->state == WLR_KEY_PRESSED && !handled) {
		// TODO execute binding
	}

	if (!handled) {
		wlr_seat_set_keyboard(wlr_seat, wlr_device);
		wlr_seat_keyboard_notify_key(wlr_seat, event->time_msec,
				event->keycode, event->state);
	}
}

static void handle_keyboard_modifiers(struct wl_listener *listener,
		void *data) {
	struct sway_keyboard *keyboard =
		wl_container_of(listener, keyboard, keyboard_modifiers);
	struct wlr_seat *wlr_seat = keyboard->seat_device->sway_seat->wlr_seat;
	struct wlr_input_device *wlr_device =
		keyboard->seat_device->input_device->wlr_device;
	wlr_seat_set_keyboard(wlr_seat, wlr_device);
	wlr_seat_keyboard_notify_modifiers(wlr_seat);
}

struct sway_keyboard *sway_keyboard_create(struct sway_seat *seat,
		struct sway_seat_device *device) {
	struct sway_keyboard *keyboard =
		calloc(1, sizeof(struct sway_keyboard));
	if (!sway_assert(keyboard, "could not allocate sway keyboard")) {
		return NULL;
	}

	keyboard->seat_device = device;
	device->keyboard = keyboard;

	wl_list_init(&keyboard->keyboard_key.link);
	wl_list_init(&keyboard->keyboard_modifiers.link);

	return keyboard;
}

void sway_keyboard_configure(struct sway_keyboard *keyboard) {
	struct xkb_rule_names rules;
	memset(&rules, 0, sizeof(rules));
	struct input_config *input_config =
		keyboard->seat_device->input_device->config;
	struct wlr_input_device *wlr_device =
		keyboard->seat_device->input_device->wlr_device;

	if (input_config && input_config->xkb_layout) {
		rules.layout = input_config->xkb_layout;
	} else {
		rules.layout = getenv("XKB_DEFAULT_LAYOUT");
	}
	if (input_config && input_config->xkb_model) {
		rules.model = input_config->xkb_model;
	} else {
		rules.model = getenv("XKB_DEFAULT_MODEL");
	}

	if (input_config && input_config->xkb_options) {
		rules.options = input_config->xkb_options;
	} else {
		rules.options = getenv("XKB_DEFAULT_OPTIONS");
	}

	if (input_config && input_config->xkb_rules) {
		rules.rules = input_config->xkb_rules;
	} else {
		rules.rules = getenv("XKB_DEFAULT_RULES");
	}

	if (input_config && input_config->xkb_variant) {
		rules.variant = input_config->xkb_variant;
	} else {
		rules.variant = getenv("XKB_DEFAULT_VARIANT");
	}

	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!sway_assert(context, "cannot create XKB context")) {
		return;
	}

	struct xkb_keymap *keymap =
		xkb_keymap_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);

	if (!keymap) {
		sway_log(L_DEBUG, "cannot configure keyboard: keymap does not exist");
		xkb_context_unref(context);
		return;
	}

	xkb_keymap_unref(keyboard->keymap);
	keyboard->keymap = keymap;
	wlr_keyboard_set_keymap(wlr_device->keyboard, keyboard->keymap);

	wlr_keyboard_set_repeat_info(wlr_device->keyboard, 25, 600);
	xkb_context_unref(context);
	struct wlr_seat *seat = keyboard->seat_device->sway_seat->wlr_seat;
	wlr_seat_set_keyboard(seat, wlr_device);

	wl_list_remove(&keyboard->keyboard_key.link);
	wl_signal_add(&wlr_device->keyboard->events.key, &keyboard->keyboard_key);
	keyboard->keyboard_key.notify = handle_keyboard_key;

	wl_list_remove(&keyboard->keyboard_modifiers.link);
	wl_signal_add( &wlr_device->keyboard->events.modifiers,
		&keyboard->keyboard_modifiers);
	keyboard->keyboard_modifiers.notify = handle_keyboard_modifiers;
}

void sway_keyboard_destroy(struct sway_keyboard *keyboard) {
	if (!keyboard) {
		return;
	}
	wl_list_remove(&keyboard->keyboard_key.link);
	wl_list_remove(&keyboard->keyboard_modifiers.link);
	free(keyboard);
}
