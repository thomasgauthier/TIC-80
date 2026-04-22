export function homedir(): string {
	return "/home/browser";
}

export function tmpdir(): string {
	return "/tmp";
}

export function platform(): string {
	return "browser";
}

export function arch(): string {
	return "x64";
}

export function hostname(): string {
	return "browser";
}

export const EOL = "\n";
export const constants = {
	signal: {} as Record<string, number>,
	errno: {} as Record<string, number>,
};
export const type = "browser";
export const release = "";
export const userInfo = () => ({
	username: "browser",
	homedir: homedir(),
	shell: "/bin/sh",
});
