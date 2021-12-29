#!/usr/bin/env python3

import json
import urllib.request
import ssl
Import("env")

ssl._create_default_https_context = ssl._create_unverified_context

params = json.dumps({
    "query": "mutation {monotonicCounter(id: \"de.kulturspektakel.contactless\") {value}}"
}).encode('utf8')
req = urllib.request.Request(
    "https://api.kulturspektakel.de/graphql",
    data=params,
    headers={'content-type': 'application/json'}
)
response = urllib.request.urlopen(req)
data = response.read()
encoding = response.info().get_content_charset('utf-8')
result = json.loads(data.decode(encoding))

env.Append(BUILD_FLAGS=[
  "-DENV_BUILD_NUMBER={}".format(result['data']['monotonicCounter']['value']),
])

# Dump construction environment (for debug purpose)
print(env.Dump())