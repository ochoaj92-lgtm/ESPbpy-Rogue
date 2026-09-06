#!/usr/bin/env python3
"""Check browser input, focus, touch, and layout against the compiled game."""

import argparse
from contextlib import contextmanager
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Thread

from playwright.sync_api import sync_playwright


@contextmanager
def site_url(explicit_url):
    if explicit_url:
        yield explicit_url
        return
    directory = Path(__file__).resolve().parents[1] / "build/web"
    if not (directory / "game.wasm").is_file():
        raise RuntimeError("Build the browser game with scripts/build_web.py first.")

    class Handler(SimpleHTTPRequestHandler):
        def log_message(self, *_args):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), partial(Handler, directory=str(directory)))
    thread = Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield f"http://127.0.0.1:{server.server_port}/"
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


def open_game(browser, url, mobile=False):
    page = browser.new_page(viewport={"width": 320 if mobile else 1280,
                                     "height": 800 if mobile else 1100},
                            has_touch=mobile, is_mobile=mobile)
    errors, logs = [], []
    page.on("pageerror", lambda error: errors.append(str(error)))
    page.on("console", lambda message: (errors if message.type == "error" else logs).append(message.text))
    page.goto(url)
    page.wait_for_function("document.getElementById('status').textContent.startsWith('Ready')")
    page.wait_for_timeout(150)
    return page, errors, logs


def snapshot(page):
    page.wait_for_timeout(100)
    return page.locator("#canvas").screenshot()


def button(page, index, touch=False):
    control = page.locator(f'button[data-key="{index}"]')
    if touch:
        control.scroll_into_view_if_needed()
        box = control.bounding_box()
        page.touchscreen.tap(box["x"] + box["width"] / 2, box["y"] + box["height"] / 2)
    else:
        control.click()
    page.wait_for_timeout(100)


def count_events(logs, name):
    return sum(message.startswith(name + " ") for message in logs)


def check_desktop(browser, url):
    page, errors, logs = open_game(browser, url)
    page.locator("#canvas").focus()
    title = snapshot(page)
    page.keyboard.press("Enter")
    intro = snapshot(page)
    assert intro != title, "Enter did not start a run"
    page.keyboard.press("Enter")
    hand = snapshot(page)
    assert hand != intro and count_events(logs, "new_run") == 1
    button(page, 4)
    assert snapshot(page) != hand, "The page Select button did not select a card"
    button(page, 6)
    assert count_events(logs, "discard") == 1, "The page Discard button did not discard once"
    button(page, 4)

    # Hold Play across its animation, then select again while it is still held.
    box = page.locator('button[data-key="7"]').bounding_box()
    page.mouse.move(box["x"] + box["width"] / 2, box["y"] + box["height"] / 2)
    page.mouse.down()
    page.wait_for_timeout(1550)
    page.keyboard.press("z")
    page.wait_for_timeout(150)
    page.mouse.up()
    assert count_events(logs, "play") == 1, "Holding Play consumed multiple hands"

    selected_hand = snapshot(page)
    page.keyboard.press("Escape")
    paused = snapshot(page)
    assert paused != selected_hand, "Escape did not pause"
    page.keyboard.press("x")
    assert snapshot(page) == selected_hand, "Pause/resume changed hand selection"

    # Enter on a focused HTML B button must not also become the game's A key.
    page.locator('button[data-key="5"]').focus()
    page.keyboard.press("Enter")
    assert snapshot(page) == paused, "Enter on the page Back button did not pause"
    button(page, 5)
    assert snapshot(page) == selected_hand, "HTML button activation also changed card selection"

    page.keyboard.down("ArrowRight")
    page.wait_for_timeout(100)
    page.locator('button[data-key="5"]').focus()
    after_blur = snapshot(page)
    page.wait_for_timeout(600)
    assert snapshot(page) == after_blur, "A held arrow remained active after canvas blur"
    page.keyboard.up("ArrowRight")
    assert not errors, "Browser errors: " + " | ".join(errors)
    page.close()


def check_mobile(browser, url):
    page, errors, logs = open_game(browser, url, mobile=True)
    width = page.evaluate("document.documentElement.scrollWidth")
    assert width <= 320, f"320px mobile viewport overflows to {width}px"
    for index in (4, 4, 3, 4, 6, 4, 7):
        button(page, index, touch=True)
    assert count_events(logs, "new_run") == 1, "Touch controls did not start a run"
    assert count_events(logs, "discard") == 1 and count_events(logs, "play") == 1
    assert page.locator("button.active").count() == 0, "Touch controls remained held after release"
    assert not errors, "Mobile browser errors: " + " | ".join(errors)
    page.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", help="Test a served site instead of starting a local server")
    parser.add_argument("--browser", help="Chromium executable; defaults to Playwright's bundled Chromium")
    args = parser.parse_args()
    with site_url(args.url) as url, sync_playwright() as playwright:
        browser = playwright.chromium.launch(executable_path=args.browser, args=["--no-sandbox"])
        try:
            check_desktop(browser, url)
            check_mobile(browser, url)
        finally:
            browser.close()
    print("Browser smoke checks passed: keyboard, pointer, focus, held keys, and 320px touch layout.")


if __name__ == "__main__":
    main()
