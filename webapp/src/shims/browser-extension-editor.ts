import {
	Container,
	Editor,
	type EditorOptions,
	type Focusable,
	getKeybindings,
	Spacer,
	Text,
	type TUI,
} from "@mariozechner/pi-tui";
import { getEditorTheme, theme } from "./browser-theme.js";

export class ExtensionEditorComponent extends Container implements Focusable {
	private editor: Editor;
	private _focused = false;

	get focused(): boolean {
		return this._focused;
	}

	set focused(value: boolean) {
		this._focused = value;
		this.editor.focused = value;
	}

	constructor(
		tui: TUI,
		_keybindings: unknown,
		title: string,
		prefill: string | undefined,
		onSubmit: (value: string) => void,
		private readonly onCancel: () => void,
		options?: EditorOptions,
	) {
		super();
		this.addChild(new Spacer(1));
		this.addChild(new Text(theme.fg("accent", title), 1, 0));
		this.addChild(new Spacer(1));
		this.editor = new Editor(tui, getEditorTheme(), options);
		this.editor.onSubmit = onSubmit;
		if (prefill) {
			this.editor.setText(prefill);
		}
		this.addChild(this.editor);
	}

	handleInput(data: string): void {
		if (getKeybindings().matches(data, "tui.select.cancel")) {
			this.onCancel();
			return;
		}
		this.editor.handleInput(data);
	}
}
