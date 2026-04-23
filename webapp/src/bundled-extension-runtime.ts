import seleneOnLuaWriteFactory from "./bundled-extensions/selene-on-lua-write.ts";
import bundledTic80LintExtensionFactory from "./bundled-extensions/tic80ctl-lint-cart-on-lua-write.ts";

import { BUNDLED_SELENE_EXTENSION_PATH, BUNDLED_TIC80_LINT_EXTENSION_PATH } from "./bundled-extension.js";

export const bundledExtensionFactories = [
	{
		path: BUNDLED_TIC80_LINT_EXTENSION_PATH,
		factory: bundledTic80LintExtensionFactory,
	},
	{
		path: BUNDLED_SELENE_EXTENSION_PATH,
		factory: seleneOnLuaWriteFactory,
	},
] as const;
