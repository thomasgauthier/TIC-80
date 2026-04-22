import { Container, Spacer, Text } from "@mariozechner/pi-tui";
import { theme } from "./browser-theme.js";

export class EarendilAnnouncementComponent extends Container {
	constructor() {
		super();
		this.addChild(new Spacer(1));
		this.addChild(new Text(theme.bold("pi has joined Earendil"), 1, 0));
		this.addChild(new Spacer(1));
		this.addChild(new Text(theme.fg("muted", "Announcement image omitted in the browser MVP."), 1, 0));
		this.addChild(new Spacer(1));
	}
}
