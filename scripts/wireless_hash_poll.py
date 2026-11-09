"""Experimental client for the disabled cooperative stage-hash endpoint."""
from __future__ import annotations

import http.client
import json
import re
import time
from urllib.parse import urlencode, urlsplit

MAX_STAGE_BYTES = 0x7fffffff - 1024


def _unique_fields(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate verification response field.')
        result[key] = value
    return result


def verify_stage(base_url, name, size, device_id, confirm_reader, *, timeout=300, cancelled=lambda: False):
    """Poll one job and return its digest.

    confirm_reader must raise if the reader changes. A timeout or lost response
    leaves the remote job state unknown; the caller owns any later inspection or
    explicit cancellation. This helper never publishes a stage.
    """
    if not isinstance(base_url, str) or any(ord(char) <= 32 or ord(char) == 127 for char in base_url):
        raise ValueError('Use the reader HTTP root address without credentials.')
    parsed = urlsplit(base_url)
    if (parsed.scheme != 'http' or not parsed.hostname or parsed.username or parsed.password
            or parsed.path not in ('', '/') or parsed.query or parsed.fragment):
        raise ValueError('Use the reader HTTP root address without credentials.')
    port = parsed.port if parsed.port is not None else 80
    if (not isinstance(name, str) or not re.fullmatch(r'__xsync_[0-9a-f]{32}\.part', name)
            or type(size) is not int or not 0 <= size <= MAX_STAGE_BYTES
            or not isinstance(device_id, str) or not re.fullmatch(r'[0-9a-f]{64}', device_id)
            or not 1 <= port <= 65535 or not callable(confirm_reader) or not callable(cancelled)
            or type(timeout) not in (int, float) or not 0 < timeout <= 3600):
        raise ValueError('Invalid stage verification request.')
    path = '/api/sync/stage-hash?' + urlencode({'name': name, 'size': size, 'deviceId': device_id})
    deadline = time.monotonic() + timeout
    confirm_reader()
    while True:
        remaining = deadline - time.monotonic()
        if cancelled() or remaining <= 0:
            raise TimeoutError('Stage verification stopped before confirmation.')
        connection = http.client.HTTPConnection(parsed.hostname, port, timeout=min(remaining, 10))
        try:
            connection.request('GET', path, headers={'Connection': 'close'})
            response = connection.getresponse()
            headers = response.getheaders()
            lengths = [value.strip() for key, value in headers if key.lower() == 'content-length']
            transfers = [value for key, value in headers if key.lower() == 'transfer-encoding']
            if (response.status not in (200, 202) or len(lengths) != 1 or transfers
                    or not re.fullmatch(r'0|[1-9][0-9]{0,2}', lengths[0]) or int(lengths[0]) > 256):
                raise ValueError('Reader did not provide a complete verification response.')
            length = int(lengths[0])
            payload = response.read(length + 1)
            if len(payload) != length:
                raise ValueError('Reader returned an incomplete verification response.')
            try:
                result = json.loads(payload, object_pairs_hook=_unique_fields)
            except (ValueError, UnicodeDecodeError) as error:
                raise ValueError('Reader returned an invalid verification response.') from error
        finally:
            connection.close()
        if cancelled() or time.monotonic() >= deadline:
            raise TimeoutError('Stage verification stopped before confirmation.')
        if response.status == 200:
            if (not isinstance(result, dict) or set(result) != {'sha256'}
                    or not isinstance(result['sha256'], str) or not re.fullmatch(r'[0-9a-f]{64}', result['sha256'])):
                raise ValueError('Reader returned an invalid digest.')
            confirm_reader()
            if cancelled() or time.monotonic() >= deadline:
                raise TimeoutError('Stage verification stopped before confirmation.')
            return result['sha256']
        if result != {'status': 'pending'}:
            raise ValueError('Reader returned an invalid pending response.')
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError('Stage verification stopped before confirmation.')
        time.sleep(min(0.25, remaining))
