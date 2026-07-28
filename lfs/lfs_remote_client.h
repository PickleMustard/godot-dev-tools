#ifndef LFS_REMOTE_CLIENT_H
#define LFS_REMOTE_CLIENT_H

#include "core/os/thread.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "scene/main/node.h"

#include "lfs_credential_provider.h"

class HTTPRequest;

// LFS Batch API client, pure C++/HTTP, no git-lfs binary involved. Must live
// in the scene tree (HTTPRequest children require it), but the binary-upload
// path runs its transfer on a background Thread via HTTPClient directly,
// since HTTPRequest::request() only accepts a String body and would corrupt
// arbitrary binary bytes.
class LfsRemoteClient : public Node {
	GDCLASS(LfsRemoteClient, Node);

protected:
	static void _bind_methods();
	void _notification(int p_what);

private:
	struct UploadJob {
		LfsRemoteClient *self = nullptr;
		String oid;
		String href;
		Dictionary header;
		String bytes_path;
	};

	Ref<LfsCredentialProvider> credential_provider;

	HTTPRequest *_batch_request = nullptr;
	HTTPRequest *_download_request = nullptr;
	String _pending_download_oid;
	String _pending_download_dest;

	// Keyed by oid rather than kept as a flat list (the original GDScript
	// used a flat Array of Threads) -- simpler to look up and join from the
	// call_deferred completion callback, which can only carry Variant-safe
	// arguments (not a raw Thread*) back across the thread boundary.
	HashMap<String, Thread *> _upload_threads;

	void _join_all_upload_threads();

	void _on_batch_request_completed(int result, int response_code, const PackedStringArray &headers, const PackedByteArray &body);
	void _on_download_request_completed(int result, int response_code, const PackedStringArray &headers, const PackedByteArray &body);
	void _on_check_objects_auth_complete(bool ok, Dictionary headers, String error, String batch_url_override, String operation, Array objects, String default_url);
	void _finish_upload(const String &oid, const Dictionary &result);

	static void _upload_thread_trampoline(void *p_userdata);
	static Dictionary _upload_via_http_client(const String &href, const Dictionary &header, const PackedByteArray &bytes);
	static PackedStringArray _headers_dict_to_array(const Dictionary &headers);

public:
	Ref<LfsCredentialProvider> get_credential_provider() const;
	void set_credential_provider(const Ref<LfsCredentialProvider> &provider);

	void setup(const Ref<LfsCredentialProvider> &provider);
	String base_batch_url(const String &remote_url) const;
	void check_objects(const String &remote_url, const Array &objects, const String &operation = "download");
	void download_object(const String &oid, const String &href, const Dictionary &header, const String &dest_path);
	void upload_object(const String &oid, const String &href, const Dictionary &header, const String &bytes_path);

	LfsRemoteClient();
	~LfsRemoteClient();
};

#endif // LFS_REMOTE_CLIENT_H
