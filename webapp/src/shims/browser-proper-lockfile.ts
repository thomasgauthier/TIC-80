type ReleaseSync = () => void;
type ReleaseAsync = () => Promise<void>;

function createSyncRelease(): ReleaseSync {
	return () => {};
}

function createAsyncRelease(): ReleaseAsync {
	return async () => {};
}

const lockfile = {
	lockSync(_path: string, _options?: unknown): ReleaseSync {
		return createSyncRelease();
	},
	async lock(_path: string, _options?: unknown): Promise<ReleaseAsync> {
		return createAsyncRelease();
	},
};

export default lockfile;
