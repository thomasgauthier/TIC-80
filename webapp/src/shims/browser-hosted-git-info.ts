export type HostedGitInfoResult = {
	domain?: string;
	user?: string;
	project?: string;
	committish?: string;
};

const hostedGitInfo = {
	fromUrl(_url: string): HostedGitInfoResult | undefined {
		return undefined;
	},
};

export default hostedGitInfo;
