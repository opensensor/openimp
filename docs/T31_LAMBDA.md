# Default AVPU lambda tables (T31/T40/T41 EP1)

EP1 offset 0 holds a 208-byte lambda table: 52 QPs x 4 one-byte lanes.
Lanes 0..2 are indexed by slice type as in AL_GetLambda (B=0, P=1, I=2),
lane 3 is the second hardware lane. OpenIMP used to carry the AVC and HEVC
default tables as byte arrays taken from libimp 1.1.6. They are now
generated from the published rate-distortion lambda model in
`src/t40/t40_lambda.h`.

## Formula

HM/JM lambda model:

    lambda(QP) = f * 2^((QP - 12) / 3)

The AVPU table stores sqrt(lambda) (the SAD-domain lambda), rounded:

    entry(QP) = max(1, R(sqrt(f) * 2^((QP - 12) / 6)))

| lane | AVC f / R      | HEVC f / R     |
|------|----------------|----------------|
| 0    | 1.00 / round   | 1.34 / floor   |
| 1    | 0.42 / ceil    | 0.42 / ceil    |
| 2    | 0.54 / ceil    | 0.54 / ceil    |
| 3    | 0.42 / ceil    | 0.42 / ceil    |

The generator uses six double constants 2^(k/6) and multiplies/halves by
two for the octave, so it needs no libm and gives the same result on the
host and on MIPS soft-float. No entry comes within 0.0013 of a rounding
boundary.

## Derivation

1. Plotting each lane against QP gives a straight line in log2 with a slope
   of exactly 1/6 per QP (values double every 6 QP: AVC lane 0 is 16, 32, 64
   at QP 36, 42, 48). That is the slope of sqrt(2^((QP-12)/3)), so the
   table holds sqrt(lambda), and the QP offset is the usual 12.
2. For each lane, scan sqrt(f) in steps of 1e-5 with floor/round/ceil and
   keep the interval that matches the most entries:
   - AVC lane 0: round, sqrt(f) in [0.99990, 1.00226], so f = 1.
   - lanes 1/3: ceil, sqrt(f) in [0.64489, 0.64892], so f in [0.4159, 0.4211].
     We use f = 0.42.
   - lane 2: ceil, sqrt(f) in [0.73438, 0.73661], so f in [0.5393, 0.5426].
     We use f = 0.54.
   - HEVC lane 0: the best fit is floor, sqrt(f) ~ 1.1575, so f = 1.34.
3. Two-stage forms (an integer lambda first, then sqrt), a free exponent
   base and fixed-point scales of 1/4/16/256 were also tried. None of them
   does better.

## Result: not fully closed-form

The formula reproduces 404 of the 416 bytes exactly. The other 12 differ by
exactly 1:

| table | QP | lane | table value | formula |
|-------|----|------|-------------|---------|
| AVC   | 37 | 1, 3 | 13          | 12      |
| HEVC  | 16 | 0    | 2           | 1       |
| HEVC  | 25 | 0    | 4           | 5       |
| HEVC  | 29 | 0    | 7           | 8       |
| HEVC  | 34 | 0    | 15          | 14      |
| HEVC  | 35 | 0    | 17          | 16      |
| HEVC  | 36 | 0    | 19          | 18      |
| HEVC  | 37 | 0    | 21          | 20      |
| HEVC  | 38 | 0    | 24          | 23      |
| HEVC  | 43 | 0    | 42          | 41      |
| HEVC  | 44 | 0    | 47          | 46      |

These differences go both up and down, so they are hand tuning and not a
rounding effect. No single f and rounding mode fits HEVC lane 0: the best
free fit (any base, any factor) still misses 7 entries.

The pure formula table has not been tried on an AVPU yet. To keep the
encoder behaviour unchanged, the generator applies these 12 entries as
explicit overrides (`t40_lambda_overrides`). The default build therefore
produces tables that are bit-identical to the validated ones. With
`-DOPENIMP_LDA_FORMULA_ONLY` the overrides are left out. Use that build only
for device experiments, until a camera run shows that the AVPU gives the
same rate and quality with it.

## Test

`tests/t31/lambda_test.c` (`make -C tests/t31 check`) builds both tables
and compares them with FNV-1a-64 hashes of the old tables. The old tables
are not kept in the tree. Their SHA-256 hashes are:

- AVC  `7958426309f35b6b248ae8deea07d0744c8f41b7c5a113398989199ae022c188`
- HEVC `f776b1d19410a265dc4935115e4e76618c166baf67f559f1b2db3c64ec2ac06e`

The test also checks that the pure formula differs only at the 12
documented overrides, by at most 1, and that every lane is monotonic.
