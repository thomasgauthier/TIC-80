export {
	access,
	copyFile,
	lstatSync as lstat,
	mkdir,
	readdir,
	readlinkSync as readlink,
	realpathSync as realpath,
	rename,
	rm,
	stat,
	unlink,
	writeFile,
} from "./fs.js";

export async function open(_path: string, _flags?: string | number): Promise<number> {
	return 1;
}

export async function readFile(path: string, encoding?: string): Promise<string | Uint8Array> {
	const { readFileSync } = await import("./fs.js");
	return readFileSync(path, encoding as string) as string | Uint8Array;
}
