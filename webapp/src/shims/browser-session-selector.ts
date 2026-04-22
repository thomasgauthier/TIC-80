import { Container, type Focusable, getKeybindings, Spacer, Text } from "@mariozechner/pi-tui";
import { theme } from "./browser-theme.js";

export class SessionSelectorComponent extends Container implements Focusable {
	private _focused = false;

	get focused(): boolean {
		return this._focused;
	}

	set focused(value: boolean) {
		this._focused = value;
	}

	constructor(
		_currentSessionsLoader: unknown,
		_allSessionsLoader: unknown,
		_onSelect: (sessionPath: string) => void,
		private readonly onCancel: () => void,
		_onExit: () => void,
		_requestRender: () => void,
	) {
		super();
		this.addChild(new Spacer(1));
		this.addChild(new Text(theme.bold("Session browser unavailable in browser MVP"), 1, 0));
		this.addChild(new Spacer(1));
		this.addChild(new Text(theme.fg("muted", "Press Escape to close."), 1, 0));
		this.addChild(new Spacer(1));
	}

	handleInput(data: string): void {
		if (getKeybindings().matches(data, "tui.select.cancel")) {
			this.onCancel();
		}
	}
}
