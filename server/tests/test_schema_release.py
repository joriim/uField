# SPDX-License-Identifier: GPL-3.0-or-later
"""Every tool names the release that ships it (ADR 0007); clients never see the tag."""

from ufield import schema as us


def test_every_tool_has_a_release_and_export_drops_it():
    doc = us.load_tools()
    for tool in doc["tools"]:
        assert us.tool_release(tool) in us.RELEASES, tool["name"]
        assert "x-ufield-release" not in us.export_tool(doc, tool)


def test_release_one_covers_the_standalone_app():
    doc = us.load_tools()
    r1 = {t["name"] for t in doc["tools"] if us.tool_release(t) == 1}
    assert {"add_observation", "attach_media", "sync_project", "add_voice_note", "read_notes", "query_features"} <= r1
    assert "request_farm_pack" not in r1 and "request_product" not in r1
