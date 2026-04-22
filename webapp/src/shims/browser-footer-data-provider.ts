export class FooterDataProvider {
	private extensionStatuses = new Map<string, string>();
	private callbacks = new Set<() => void>();
	private availableProviderCount = 0;

	getGitBranch(): string | null {
		return null;
	}

	getExtensionStatuses(): ReadonlyMap<string, string> {
		return this.extensionStatuses;
	}

	onBranchChange(callback: () => void): () => void {
		this.callbacks.add(callback);
		return () => this.callbacks.delete(callback);
	}

	setExtensionStatus(key: string, text: string | undefined): void {
		if (text === undefined) {
			this.extensionStatuses.delete(key);
		} else {
			this.extensionStatuses.set(key, text);
		}
	}

	clearExtensionStatuses(): void {
		this.extensionStatuses.clear();
	}

	getAvailableProviderCount(): number {
		return this.availableProviderCount;
	}

	setAvailableProviderCount(count: number): void {
		this.availableProviderCount = count;
	}

	setCwd(_cwd: string): void {}

	dispose(): void {
		this.callbacks.clear();
	}
}
