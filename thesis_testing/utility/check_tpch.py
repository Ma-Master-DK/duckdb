import re
import sys


def normalize_value(val: str) -> str:
    if re.fullmatch(r"-?\d+\.00", val):
        return val[:-3]
    return val


def parse_actual_output(raw_output: str) -> str:
    lines = raw_output.strip().split("\n")
    lines.pop()
    lines.pop(3)
    lines.pop(2)
    lines.pop(0)
    rows = []
    for line in lines:
        parts = [normalize_value(part.strip()) for part in line.strip("│").split("│")]
        parts = [re.sub(r"\(.*?\)", "", p).strip() for p in parts]
        rows.append("|".join(parts))
    return "\\n".join(rows) + "\\n"


def extract_expected_answer(raw_expected: str) -> str:
    line = raw_expected.strip().split("\n")[4]
    parts = [p.strip() for p in line.strip("│").split("│")]
    return parts[2]


def compare_outputs(actual: str, expected: str) -> bool:
    return actual.strip() == expected.strip()


def main():
    if len(sys.argv) != 3:
        print("Usage: python3 check_tpch.py output.txt answer.txt")
        sys.exit(1)

    output_file = sys.argv[1]
    answer_file = sys.argv[2]

    with open(output_file, "r", encoding="utf-8") as f:
        raw_output = f.read()

    with open(answer_file, "r", encoding="utf-8") as f:
        raw_answer = f.read()

    actual_str = parse_actual_output(raw_output)
    expected_str = extract_expected_answer(raw_answer)

    if compare_outputs(actual_str, expected_str):
        sys.exit(0)
    else:
        sys.exit(2)


if __name__ == "__main__":
    main()
