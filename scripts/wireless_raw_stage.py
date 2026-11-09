"""Experimental raw staging transport; the caller owns journaling and verification."""
from __future__ import annotations

import hashlib
import http.client
import json
import re
from urllib.parse import urlsplit

MAX_SIZE = 0x7fffffff - 1024


def _unique_fields(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate field in staging response.')
        result[key] = value
    return result


def upload_stage(base_url, port, name, device_id, source, size, expected_sha256, *, timeout=30):
    """Send one stage once. On any error its remote state is unknown; do not retry blindly."""
    parsed = urlsplit(base_url)
    if (parsed.scheme != 'http' or not parsed.hostname or parsed.username or parsed.password
            or parsed.path not in ('', '/') or parsed.query or parsed.fragment):
        raise ValueError('Use the reader HTTP root address without credentials.')
    if (type(port) is not int or not 1 <= port <= 65535
            or not isinstance(name, str) or not re.fullmatch(r'__xsync_[0-9a-f]{32}\.part', name)
            or not isinstance(device_id, str) or not re.fullmatch(r'[0-9a-f]{64}', device_id)
            or type(size) is not int or not 0 <= size <= MAX_SIZE
            or not isinstance(expected_sha256, str) or not re.fullmatch(r'[0-9a-f]{64}', expected_sha256)):
        raise ValueError('Invalid stage request.')
    # Validate the original URL port even though the staging listener has its own port.
    _ = parsed.port
    connection = http.client.HTTPConnection(parsed.hostname, port, timeout=timeout)
    try:
        connection.putrequest('PUT', '/api/sync/stage-raw', skip_accept_encoding=True)
        connection.putheader('Content-Type', 'application/octet-stream')
        connection.putheader('Content-Length', str(size))
        connection.putheader('X-Xsync-Device', device_id)
        connection.putheader('X-Xsync-Stage', name)
        connection.putheader('Connection', 'close')
        connection.endheaders()
        digest = hashlib.sha256()
        remaining = size
        while remaining:
            data = source.read(min(65536, remaining))
            if not isinstance(data, bytes) or not data or len(data) > remaining:
                raise ValueError('Source changed or could not be read; stage state is unknown.')
            connection.send(data)
            digest.update(data)
            remaining -= len(data)
        trailing = source.read(1)
        if not isinstance(trailing, bytes) or trailing or digest.hexdigest() != expected_sha256:
            raise ValueError('Source size or hash changed; stage state is unknown.')
        response = connection.getresponse()
        headers = response.getheaders()
        lengths = [value.strip() for key, value in headers if key.lower() == 'content-length']
        transfers = [value for key, value in headers if key.lower() == 'transfer-encoding']
        if response.status != 201 or lengths != ['15'] or transfers:
            raise ValueError('Reader did not confirm staging; stage state is unknown.')
        payload = response.read(16)
        if len(payload) != 15:
            raise ValueError('Reader did not confirm staging; stage state is unknown.')
        try:
            result = json.loads(payload, object_pairs_hook=_unique_fields)
        except (ValueError, UnicodeDecodeError) as error:
            raise ValueError('Invalid staging response; stage state is unknown.') from error
        if not isinstance(result, dict) or set(result) != {'staged'} or result['staged'] is not True:
            raise ValueError('Invalid staging response; stage state is unknown.')
    finally:
        connection.close()
