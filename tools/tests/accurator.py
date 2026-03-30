import random
import time


def expression(tuner_state):
    process_logs = []
    total_process_ms = 0.0
    workload = tuner_state["workload"]

    line_ms = 0.0
    repeats = 0
    random_number = 0
    while line_ms < tuner_state["min_line_ms"] and repeats < tuner_state["max_repeats"]:
        start = time.process_time()
        random_number = random.randint(1, 10)
        line_ms += (time.process_time() - start) * 1000
        repeats += 1
    total_process_ms += line_ms
    process_logs.append(
        f"L1 random_number={random_number} ({line_ms:.6f} ms, repeats={repeats})"
    )

    line_ms = 0.0
    repeats = 0
    random_number_squared = 0
    while line_ms < tuner_state["min_line_ms"] and repeats < tuner_state["max_repeats"]:
        start = time.process_time()
        random_number_squared = random_number ** 2
        line_ms += (time.process_time() - start) * 1000
        repeats += 1
    total_process_ms += line_ms
    process_logs.append(
        "L2 random_number_squared="
        f"{random_number_squared} ({line_ms:.6f} ms, repeats={repeats})"
    )

    line_ms = 0.0
    repeats = 0
    generated_values = []
    while line_ms < tuner_state["min_line_ms"] and repeats < tuner_state["max_repeats"]:
        start = time.process_time()
        generated_values = [((index * random_number) % 97) - 48 for index in range(workload)]
        line_ms += (time.process_time() - start) * 1000
        repeats += 1
    total_process_ms += line_ms
    process_logs.append(
        "L3 generated_values_len="
        f"{len(generated_values)} ({line_ms:.6f} ms, repeats={repeats})"
    )

    line_ms = 0.0
    repeats = 0
    clamped_values = []
    while line_ms < tuner_state["min_line_ms"] and repeats < tuner_state["max_repeats"]:
        start = time.process_time()
        clamped_values = [max(-25, min(25, value)) for value in generated_values]
        line_ms += (time.process_time() - start) * 1000
        repeats += 1
    total_process_ms += line_ms
    process_logs.append(
        "L4 clamped_values_len="
        f"{len(clamped_values)} ({line_ms:.6f} ms, repeats={repeats})"
    )

    line_ms = 0.0
    repeats = 0
    folded_energy = 0
    while line_ms < tuner_state["min_line_ms"] and repeats < tuner_state["max_repeats"]:
        start = time.process_time()
        folded_energy = sum((value * value) + abs(value) for value in clamped_values)
        line_ms += (time.process_time() - start) * 1000
        repeats += 1
    total_process_ms += line_ms
    process_logs.append(
        f"L5 folded_energy={folded_energy} ({line_ms:.6f} ms, repeats={repeats})"
    )

    line_ms = 0.0
    repeats = 0
    oscillation = 0
    clamped_len = len(clamped_values)
    while line_ms < tuner_state["min_line_ms"] and repeats < tuner_state["max_repeats"]:
        start = time.process_time()
        oscillation = sum(
            (index % 7) * clamped_values[index % clamped_len] for index in range(workload)
        )
        line_ms += (time.process_time() - start) * 1000
        repeats += 1
    total_process_ms += line_ms
    process_logs.append(
        f"L6 oscillation={oscillation} ({line_ms:.6f} ms, repeats={repeats})"
    )

    line_ms = 0.0
    repeats = 0
    raw_result = 0
    while line_ms < tuner_state["min_line_ms"] and repeats < tuner_state["max_repeats"]:
        start = time.process_time()
        raw_result = (
            folded_energy
            + oscillation
            + (2 * random_number_squared)
            + (3 * random_number)
            + 5
        )
        line_ms += (time.process_time() - start) * 1000
        repeats += 1
    total_process_ms += line_ms
    process_logs.append(f"L7 raw_result={raw_result} ({line_ms:.6f} ms, repeats={repeats})")

    line_ms = 0.0
    repeats = 0
    tuned_result = 0
    while line_ms < tuner_state["min_line_ms"] and repeats < tuner_state["max_repeats"]:
        start = time.process_time()
        tuned_result = round(
            min(raw_result, tuner_state["max_return"]), tuner_state["round_digits"]
        )
        line_ms += (time.process_time() - start) * 1000
        repeats += 1
    total_process_ms += line_ms
    process_logs.append(
        "L8 tuned_result="
        f"{tuned_result} limit={tuner_state['max_return']} "
        f"round_digits={tuner_state['round_digits']} workload={workload} "
        f"({line_ms:.6f} ms, repeats={repeats})"
    )

    return tuned_result, process_logs, total_process_ms


def downtuner(tuner_state):
    tuner_state["max_return"] = max(20, int(tuner_state["max_return"] * 0.9))
    tuner_state["round_digits"] = 0
    tuner_state["workload"] = max(2_000, int(tuner_state["workload"] * 0.75))
    tuner_state["min_line_ms"] = max(0.5, tuner_state["min_line_ms"] * 0.85)
    return tuner_state


def accurator(x_ms=2.0, iterations=10, sleep_seconds=0.2):
    tuner_state = {
        "max_return": 5_000_000,
        "round_digits": 2,
        "workload": 40_000,
        "min_line_ms": 1.5,
        "max_repeats": 10,
    }
    cumulative_process_ms = 0.0

    for index in range(1, iterations + 1):
        result, process_logs, final_process_ms = expression(tuner_state)
        cumulative_process_ms += final_process_ms

        print(f"\nRun {index}")
        for log in process_logs:
            print(log)
        print(f"Final process time: {final_process_ms:.6f} ms")
        print(f"Cumulative process time: {cumulative_process_ms:.6f} ms")
        print(f"Result: {result}")

        if final_process_ms > x_ms:
            print(
                f"request 'downtuner' because {final_process_ms:.6f} ms > {x_ms:.6f} ms"
            )
            tuner_state = downtuner(tuner_state)
            print(f"downtuner applied: {tuner_state}")

        time.sleep(sleep_seconds)


if __name__ == "__main__":
    accurator()