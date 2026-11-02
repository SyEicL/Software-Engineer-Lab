"""Black-box CLI workflow; Python 3 standard library only, no real payments."""

import argparse
import re
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    executable = str(args.executable.resolve())
    with tempfile.TemporaryDirectory(prefix="market-cli-") as folder:
        data = str(Path(folder) / "中文数据.db")

        def run(commands, *, phone=None, init=False):
            prefix = [] if not phone else [f"login {phone}", "Demo12345"]
            command = [executable, "--data", data]
            if init:
                command += ["--init-admin", "13800000001"]
                prefix = ["Demo12345"]
            result = subprocess.run(
                command,
                input="\n".join(prefix + commands + ["quit", ""]),
                capture_output=True,
                encoding="utf-8",
                timeout=30,
                check=True,
            )
            assert "Demo12345" not in result.stdout, "Password leaked to output"
            return result.stdout

        def good(commands, phone=None, init=False):
            output = run(commands, phone=phone, init=init)
            assert "ERROR" not in output, output
            return output

        good(["register 13800000002 卖家", "Demo12345",
              "register 13800000003 买家", "Demo12345"], init=True)
        output = good(["users"], "13800000001")
        seller = int(re.search(r"(\d+) \| 卖家 \|", output)[1])
        good(["apply-seller"], "13800000002")
        good([f"review-seller {seller} approve"], "13800000001")
        output = good(['draft "C++ 教材" 图书 2800 "保存中文和空格"'],
                      "13800000002")
        product = int(re.search(r"Product ID: (\d+)", output)[1])
        good([f"submit {product}"], "13800000002")
        good([f"review-product {product} approve"], "13800000001")
        output = good(['search "C++" 图书 2000 3000'])
        assert "C++ 教材" in output
        version = int(re.search(r"version=(\d+)", output)[1])
        good([f"favorite {product}", f"favorite {product}",
              f"add-cart {product}", f"add-cart {product}"], "13800000003")
        output = good(["cart", "favorites"], "13800000003")
        assert output.count("C++ 教材") == 2
        assert "No products." in good(["cart", "favorites"], "13800000001")
        output = good([f"reserve {product} {version} reserve-demo"],
                      "13800000003")
        reservation = int(re.search(r"Reservation ID: (\d+)", output)[1])
        output = good(["cart", "favorites"], "13800000003")
        assert "No products." in output and output.count("C++ 教材") == 1
        good([f"unfavorite {product}", f"remove-cart {product}", "clear-cart"],
             "13800000003")
        good([f"confirm {reservation} 1"], "13800000002")
        output = good([f'order {reservation} "学校 宿舍"'], "13800000003")
        order = int(re.search(r"Order ID: (\d+)", output)[1])
        good([f"pay {order} payment-demo"], "13800000003")
        good([f"ship {order} TRACK-DEMO"], "13800000002")
        output = good([f"receive {order}", "orders"], "13800000003")
        assert "completed" in output
        output = good(["stats"], "13800000002")
        assert "completed: 1 | cents: 2800" in output
        output = run([f"pay {order} new-payment"], phone="13800000003")
        assert "ERROR 4" in output
        output = run(["reserve -1 1 key", "search x x 0 9 extra"])
        assert output.count("ERROR 0") == 2
        print("PASS: CLI purchase workflow across restarts, UTF-8 path/text, "
              "2800-cent statistics, terminal-state guard, strict input parsing")


if __name__ == "__main__":
    main()
