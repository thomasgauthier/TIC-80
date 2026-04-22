import type { PhotonImage as PhotonImageType } from "@silvia-odwyer/photon";

type PhotonInit = (moduleOrPath?: unknown) => Promise<unknown>;
type PhotonModule = Omit<typeof import("@silvia-odwyer/photon"), "default">;

let photonModule: PhotonModule | null = null;
let loadPromise: Promise<PhotonModule | null> | null = null;

export type { PhotonImageType };

export async function loadPhoton(): Promise<PhotonModule | null> {
	if (photonModule) {
		return photonModule;
	}

	if (loadPromise) {
		return loadPromise;
	}

	loadPromise = (async () => {
		try {
			const module = await import("@silvia-odwyer/photon");
			const init = module.default as unknown as PhotonInit;
			await init();
			const { default: _default, ...exports } = module;
			photonModule = exports;
			return photonModule;
		} catch {
			photonModule = null;
			return photonModule;
		}
	})();

	return loadPromise;
}
