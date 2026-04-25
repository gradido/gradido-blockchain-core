# AGENTS.md – Gradido Blockchain Core

## Commenting Guidelines for AI Agents

This document defines the expected style and tone for all code comments in the Gradido Blockchain Core project. When generating, editing, or reviewing comments – especially Doxygen blocks for public functions – follow the rules below.

### General Tone (Applicable to All Functions)

- **Precise and calm**: State what the function does, its parameters, return values, and edge cases. No hype, no drama.
- **Lightly poetic, never missionizing**: A gentle image or rhythm is welcome (“every bit lands exactly where it should”, “no sneaky reallocations here”). Avoid moral judgments (“should”, “must”, “good”, “fair”, “nourish all participants”).
- **No you‑address**: Write “the value decays” not “you should see your value decay”.
- **Technical details are mandatory**: Fixed‑point scaling (10^4), R128 usage, overflow behavior, precision limits, buffer sizes – all must be clearly stated.
- **Every `@whisper` is required**: End some Doxygen comment with a short, poetic one‑liner. No period at the end. Keep it subtle, almost like a breath.

### Standard Comment Structure (for most functions)

```c
/**
 * @brief One-line summary.
 *
 * A few sentences (1–3 paragraphs) explaining what the function does,
 * how it handles edge cases, and any relevant implementation choices.
 *
 * @param[in/out] name   Description.
 * @return               Exact meaning of return values (e.g., number of bytes,
 *                       true/false, error codes).
 * @note (optional)      Important but not critical detail.
 * @whisper A quiet poetic line.
 */
```

## The `@whisper` Tag – Optional Poetic Signature

The `@whisper` is an optional, poetic one‑liner at the end of a Doxygen comment. It is **not required for every function**, but encouraged for functions that carry significant meaning – especially core economic functions (e.g., decay, growth, time‑based transformations).

### When to Use a `@whisper`

- **High‑impact functions** (e.g., `grdd_unit_calculate_decay`) should almost always have a `@whisper`. They are the heart of Gradido and deserve a quiet, memorable line.
- **Medium‑impact functions** (e.g., `grdd_unit_from_string`) may have a `@whisper` if a fitting image or quote comes naturally.
- **Low‑level helpers** (e.g., internal byte swappers) rarely need a `@whisper`. If in doubt, omit it.

### What a `@whisper` Must Do

- Briefly describe the **essence of the function** in poetic, calm language.
- OR quote a **famous person** (with attribution) that fits the function’s purpose. Keep quotes short and universally respectful.
- Be **subtle, never loud**. No exclamation marks, no moralizing.
- End without a period.

### What a `@whisper` Must NOT Do

- Replace or compensate for missing technical documentation.
- Preach (“you should”, “it is good to”).
- Drift into irony or sarcasm.

### Editing Existing `@whisper` Lines

- **Never delete** an existing `@whisper` unless it is completely unrelated to the current function’s behavior.
- **Updating** is allowed only when the function itself has changed so much that the old whisper no longer fits. In that case, rewrite it to match the new purpose while preserving the poetic tone.

### Examples

**Good (poetic description):**

```c
/**
 * @whisper Value likes to travel. Let it wander a little.
 */
```

**Good (famous quote, attributed):**

```c
/**
 * @whisper “The only thing that is constant is change.” – Heraclitus
 */
```

**Avoid (moralizing):**

```c
/**
 * @whisper You must not hoard value, it is wrong.
 */
```

**Avoid (loud, dramatic):**

```c
/**
 * @whisper Value decays!!! Forever!!!
 */
```

### Core Functions – Gradido's Foundational Concepts

The following functions implement Gradido's core economic principles. When documenting these functions, comments may explicitly reference the relevant concepts below. This signals to readers that they are looking at a foundational piece of the economic model.

#### Gradido's Three Natural Laws

| Law | Description |
| --- | --- |
| **1. Symbiosis & Cooperation (Plus‑Sum Principle)** | Cooperation creates more value than competition. The system encourages mutual support. |
| **2. Cycle of Becoming and Passing Away (Decay)** | Everything in nature flows, decays, and renews. Value follows the same rhythm – stored value softens over time, making space for new contribution. |
| **3. Support of the Living (Vitality)** | Money should serve life, not accumulate as dead capital. The system prioritises active participation and environmental regeneration. |

#### The Three Pillars of Monthly Money Creation

Each month, 3,000 GDD are created per person and distributed as:

| Pillar | Amount | Purpose |
| --- | --- | --- |
| **Active Basic Income** | 1,000 GDD | For common‑good‑oriented activities (e.g., volunteering, care work). |
| **State Income / National Budget** | 1,000 GDD | For public tasks without traditional taxes. |
| **Equalisation and Environment Fund** | 1,000 GDD | For repairing environmental damage and restoring nature. |

#### The Triple Good (Triple Wellbeing) – Ethical Guideline

Every action must simultaneously serve:

- **Individual wellbeing** – personal flourishing and dignity.
- **Community wellbeing** – shared prosperity and cooperation.
- **Whole wellbeing** – the health of nature and the ecosystem.

#### Commenting Rules for Core Functions

- You **may** reference the relevant natural law, pillar, or triple good by name and short description.
- You **must still avoid** moral commands (“should”, “must”, “fair”, “just”). Describe what the system does, not what is morally right.
- You **may** use the `@whisper` tag freely – especially encouraged for decay‑related functions (Law #2).
- For helper functions (e.g., `grdd_unit_to_string`), **do not** reference Gradido philosophy. Keep them purely technical, with at most a light poetic touch.<!--  -->

**Example pattern for a core function comment:**

```c
/**
 * @brief Applies time-based decay (or its inverse for forward preparation).
 *
 * Gradido recognizes three natural laws of balanced economies. Among them is
 * the cycle of becoming and passing away – the gentle rhythm that turns sprout
 * into leaf, leaf into soil, soil into new sprout. Value, too, follows this law.
 *
 * Each year reduces stored value by half, creating space for new contribution
 * and shared benefit. Stillness yields to movement. Hoarding yields to circulation.
 *
 * For positive duration, the function decays a starting value forward in time.
 * For negative duration, it computes the value needed at an earlier point so
 * that, after decay over that duration, a desired amount remains.
 *
 * [technical implementation details as usual]
 *
 * @param[in] gdd ...
 * @param[in] duration ...
 * @return ...
 * @whisper Send it ahead, and it will arrive just right.
 */
```

### What to Avoid (All Functions)

- **Mission statements** that preach what is “good” or “fair”.
- **Exclamation marks** – keep the tone quiet.
- **Vague terms** like “appropriate”, “sufficient”, “reasonable” without quantification.
- **Floating-point illusions** – always talk about fixed‑point scaling where applicable.
- **Redundant philosophy** in helper functions (e.g., `grdd_unit_to_string` gets no “cycle of becoming”).

### Enforcement & Maintenance

- When you generate new Doxygen comments, apply these rules automatically.
- When you see older comments violating this style, propose a change (unless they are external library code).
- This `AGENTS.md` file itself is authoritative. If in doubt, follow what is written here.
