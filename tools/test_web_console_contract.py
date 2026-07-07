from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="ignore")


def require(text: str, needle: str, message: str) -> None:
    if needle not in text:
        raise AssertionError(message)


def main() -> None:
    http_server = read_text("main/http_server.c")
    main_c = read_text("main/main.c")

    for uri in ('"/capture.jpg"', '"/api/config"', '"/api/analyze"'):
        require(http_server, uri, f"missing web console URI {uri}")

    for field in ("apiUrl", "apiKey", "model", "uploadUrl", "prompt"):
        require(http_server, field, f"missing configurable field {field}")

    for api_marker in ("Authorization", "image_url", "esp_crt_bundle_attach"):
        require(http_server, api_marker, f"missing direct cloud API marker {api_marker}")

    require(main_c, "start_http_server()", "main app does not start the web console HTTP server")
    require(main_c, "start_mdns_service()", "main app does not start mDNS for browser access")


if __name__ == "__main__":
    main()
