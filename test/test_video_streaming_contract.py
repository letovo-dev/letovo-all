from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MEDIA_CC = ROOT / "src" / "basic" / "media.cc"
LAZY_VIDEO_TSX = ROOT / "frontend" / "src" / "entities" / "post" / "ui" / "LazyVideo.tsx"


def test_media_endpoint_supports_byte_range_responses():
    source = MEDIA_CC.read_text()

    assert 'req->header().get_field("Range")' in source
    assert "parse_range_header(range_header, file_size)" in source
    assert "restinio::status_partial_content()" in source
    assert "restinio::http_field::content_range" in source
    assert "restinio::http_field::accept_ranges" in source
    assert "offset_and_size(" in source
    assert "restinio::status_requested_range_not_satisfiable()" in source
    assert '"bytes */" + std::to_string(file_size)' in source


def test_lazy_video_uses_native_streaming_instead_of_blob_prefetch():
    source = LAZY_VIDEO_TSX.read_text()

    assert "fetch(src" not in source
    assert "response.blob()" not in source
    assert "URL.createObjectURL" not in source
    assert "useVideoSessionCache" not in source
    assert "preload = 'metadata'" in source
    assert "src={src}" in source
    assert "poster={poster}" in source
    assert "playsInline" in source
