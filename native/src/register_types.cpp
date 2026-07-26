#include "register_types.h"

#include "git/git_backend.h"

#include <gdextension_interface.h>

#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include <git2.h>

using namespace godot;

void initialize_git_backend_types(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_EDITOR) {
		return;
	}

	git_libgit2_init();

	GDREGISTER_CLASS(GitBackend);
}

void uninitialize_git_backend_types(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_EDITOR) {
		return;
	}

	git_libgit2_shutdown();
}

extern "C" {
GDExtensionBool GDE_EXPORT git_backend_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

	init_obj.register_initializer(initialize_git_backend_types);
	init_obj.register_terminator(uninitialize_git_backend_types);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_EDITOR);

	return init_obj.init();
}
}
