# Healthcare Appointment System — C Version (v2)

C implementation of the DAA PBL project **Healthcare Appointment System**.

Central DAA concepts:

- **Binary Search Tree (upgraded to a self-balancing AVL tree):** each doctor's date-specific *free* slots are keyed by minutes-from-midnight. Inorder traversal gives chronological order; the BST minimum gives the earliest free slot.
- **Separate-Chaining Hash Maps (auto-rehashing):** patients, doctors, appointments, day-schedules and a patient/time clash index.
- **Mutex-based scheduling:** a per-doctor/date mutex wraps the BST check-and-delete, so two threads can never book the same slot.

## Compile and run

```bash
gcc -O2 -Wall -Wextra healthcare_appointment.c -o healthcare -pthread
./healthcare              # interactive menu
./healthcare --selftest   # 49 automated correctness + concurrency checks
```

Windows: use MinGW/MSYS2 (or WSL) with pthread support, same command with `healthcare.exe`.

## Menu

| # | Action |
|---|--------|
| 1 | Register patient (validated) |
| 2 | Search patient (hash map) |
| 3 | Show patients (sorted by ID) |
| 4 | Show doctors |
| 5 | Show available slots (AVL inorder + tree height) |
| 6 | Book appointment |
| 7 | Cancel appointment |
| 8 | Reschedule appointment (atomic) |
| 9 | Show appointments (sorted by ID) |
| 10 | Double-booking concurrency demo |
| 11 | Show one patient's appointments *(new)* |
| 12 | Find a doctor's next available slot *(new)* |
| 0 | Exit *(was 11)* |

## What changed from v1

**Bugs fixed**

| Bug in v1 | Fix |
|-----------|-----|
| Booking always failed with "Invalid time format": `date[11]` held exactly 10 chars, so `fgets` left the newline in the buffer and the *next* prompt read an empty line. | `read_line` discards the rest of an over-long line; input buffers are larger. |
| Running the concurrency demo twice force-freed a booked slot and produced **two CONFIRMED appointments for one slot**. | Demo never forces a slot free; it uses 10:00 AM or the earliest free slot. |
| Impossible dates (e.g. `2026-02-30`) were accepted. | Strict calendar validation (leap years included); dates are canonicalised. |
| EOF on stdin made the menu loop forever. | EOF exits cleanly. |
| `2:00 PM` stored with a different label than `02:00 PM`. | Times are normalised to `hh:mm AM/PM`. |
| Booking a non-slot time (e.g. 09:30) said "already booked". | Separate "not an offered slot" error. |
| Global appointment counter and hash maps were not thread-safe across different schedules. | Every hash map has its own lock; counter is locked. |
| Cancel/reschedule checked the status *outside* the lock (double-cancel race). | Per-appointment lock, with a documented lock order (no deadlock). |

**Improvements**

- **AVL balancing:** worst case is now O(log n) instead of O(n) for a skewed BST (2000 sorted inserts → height ≤ 15).
- **Hash maps resize** at load factor 0.75; schedules are looked up by hash (`DOCTOR|DATE`) instead of a linear scan with a fixed 200-entry cap.
- **Patient clash detection:** a patient cannot hold two appointments at the same date/time with different doctors; the check is atomic.
- IDs are case/space-insensitive (`p101`, `d201`), listings are sorted, errors are specific.
- All 7 hourly slots are real 60-minute slots (v1 labelled them 30).

## Testing

`./healthcare --selftest` covers date/time helpers, AVL balance, hash-map rehashing, booking rules, cancel/reschedule, and real-thread concurrency:

- 56 threads fight for 7 slots → exactly 7 bookings
- 8 threads cancel the same appointment → exactly 1 succeeds
- 1 patient, 3 doctors, same time → exactly 1 booking, no leaked slots
- 12 threads × 3000 random book/cancel/reschedule ops → invariant `free slots + confirmed = 7` holds for every doctor/date

The suite was also run clean under AddressSanitizer, UBSan and ThreadSanitizer, and was checked with deliberately broken ("mutant") versions of the code (mutex removed, non-atomic clash check) to confirm it fails when the locking is wrong.

## Complexity

- Hash map get/put/remove: O(1) expected (amortised, with rehashing).
- AVL search/insert/delete: **O(log n) worst case**.
- Inorder schedule display: O(n). Earliest free slot: O(log n).
- Booking, cancellation: O(log n) + O(1) expected hash operations.
- Rescheduling: atomic across the (at most two) involved schedule locks.
- "Show one patient's appointments" scans all appointments: O(A).

## Notes

- The C standard library has no portable mutex, so POSIX `pthread_mutex_t` is used.
- In-memory simulation (as in the report's scope); data is not persisted between runs.
- **Lock order:** appointment lock → schedule lock(s), lower address first → hash-map/counter locks (leaf locks).
