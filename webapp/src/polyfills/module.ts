export function createRequire(_from: string | URL) {
	return function require(_id: string): unknown {
		throw new Error(`Cannot require "${_id}" in browser environment`);
	};
}
