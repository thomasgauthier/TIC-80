export class DefaultPackageManager {
	async checkForAvailableUpdates(): Promise<Array<{ displayName: string }>> {
		return [];
	}
}
