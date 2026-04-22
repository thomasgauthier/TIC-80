/// <reference lib="dom" />

import { Container, type Focusable, getKeybindings, Input, Spacer, Text, type TUI } from "@mariozechner/pi-tui";
import { theme } from "./browser-theme.js";

export class LoginDialogComponent extends Container implements Focusable {
	private contentContainer = new Container();
	private input = new Input();
	private abortController = new AbortController();
	private resolver?: (value: string) => void;
	private rejecter?: (error: Error) => void;
	private _focused = false;

	get focused(): boolean {
		return this._focused;
	}

	set focused(value: boolean) {
		this._focused = value;
		this.input.focused = value;
	}

	constructor(
		private readonly tui: TUI,
		providerId: string,
		private readonly onComplete: (success: boolean, message?: string) => void,
	) {
		super();
		this.addChild(new Text(theme.fg("warning", `Login to ${providerId}`), 1, 0));
		this.addChild(this.contentContainer);
		this.input.onSubmit = () => {
			const value = this.input.getValue();
			this.resolver?.(value);
			this.resolver = undefined;
			this.rejecter = undefined;
		};
	}

	get signal(): AbortSignal {
		return this.abortController.signal;
	}

	private cancel(): void {
		this.abortController.abort();
		this.rejecter?.(new Error("Login cancelled"));
		this.resolver = undefined;
		this.rejecter = undefined;
		this.onComplete(false, "Login cancelled");
	}

	showAuth(url: string, instructions?: string): void {
		this.contentContainer.clear();
		this.contentContainer.addChild(new Spacer(1));
		this.contentContainer.addChild(new Text(theme.fg("accent", url), 1, 0));
		if (instructions) {
			this.contentContainer.addChild(new Spacer(1));
			this.contentContainer.addChild(new Text(theme.fg("muted", instructions), 1, 0));
		}
		window.open(url, "_blank", "noopener,noreferrer");
		this.tui.requestRender();
	}

	showManualInput(prompt: string): Promise<string> {
		return this.showPrompt(prompt);
	}

	showPrompt(message: string): Promise<string> {
		this.contentContainer.addChild(new Spacer(1));
		this.contentContainer.addChild(new Text(theme.fg("text", message), 1, 0));
		this.contentContainer.addChild(this.input);
		this.tui.requestRender();
		return new Promise((resolve, reject) => {
			this.resolver = resolve;
			this.rejecter = reject;
		});
	}

	showWaiting(message: string): void {
		this.contentContainer.addChild(new Spacer(1));
		this.contentContainer.addChild(new Text(theme.fg("muted", message), 1, 0));
		this.tui.requestRender();
	}

	showProgress(message: string): void {
		this.contentContainer.addChild(new Text(theme.fg("muted", message), 1, 0));
		this.tui.requestRender();
	}

	handleInput(data: string): void {
		if (getKeybindings().matches(data, "tui.select.cancel")) {
			this.cancel();
			return;
		}
		this.input.handleInput(data);
	}
}
