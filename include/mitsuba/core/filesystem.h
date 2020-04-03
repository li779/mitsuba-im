#pragma once
#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING
#include <experimental/filesystem>

namespace fs {

	using namespace std::experimental::filesystem;

	//struct pathref {
	//	path const& p;
	//	pathref(path const& p) : p(p) { }
	//	operator path const&() const { return p; }
	//};

	inline pathstr encode_pathstr(path const& p) {
		return pathstr(p.native());
	}
	MTS_EXPORT_CORE path decode_pathstr(pathstr const& p);

	struct pathdat {
		path p;
		pathdat() = default;
		pathdat(path p) : p((path&&) p) { }
		operator path&() { return p; }
		operator path const&() const { return p; }
//		operator pathref() const { return p; }
	};

}
