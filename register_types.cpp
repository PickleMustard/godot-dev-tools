#include "register_types.h"

#include "git/git_backend.h"

#include "core/object/class_db.h"

#include <git2.h>

void initialize_dev_tools_git_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_EDITOR) {
		return;
	}

	git_libgit2_init();

	GDREGISTER_CLASS(GitBackend);
}

void uninitialize_dev_tools_git_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_EDITOR) {
		return;
	}

	git_libgit2_shutdown();
}
