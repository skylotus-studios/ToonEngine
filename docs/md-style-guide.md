<!-- tidy-md:locked — hand-authored prose-quality rules; revise deliberately, not via routine tidying -->

# ToonEngine MD Style Guide

These patterns are symptoms, not the disease. The real defect is vague, unsourced, importance-inflating writing with no specific reader in mind. Stripping the surface tics without fixing the substance only makes the output harder to catch. Fix the substance first.

---

## 1. Language and Tone

### Significance Inflation (Puffery About Importance)
Do not tell the reader a thing matters. State what it does and let them judge. This is the most pervasive tell.

- Banned: "stands as a testament to", "plays a vital/significant/crucial role", "marks a pivotal moment", "represents a broader shift", "underscores the importance of", "has cemented its place as", "this matters because".
- Better: the concrete fact. Not "The release marked a pivotal moment for the studio," but "The release was the studio's first title to sell over a million copies."

### Present-Participle / Trailing Importance Clauses
Sentences that end with a vague `-ing` clause asserting relevance.

- Banned tails: "...emphasizing the significance of...", "...reflecting its enduring relevance", "...highlighting the broader impact", "...solidifying its position as...", "...cementing its legacy", "...making it a cornerstone of...".
- Better: cut the clause or replace it with a real consequence. "...which doubled active users in three months." If you can't name a concrete result, you have no reason to claim significance.

### Fake Candor
Confiding openers that add nothing.

- Banned: "Honestly,", "And honestly?", "Honestly?", "To be honest,", "Look,", "Here's the thing,", "I'll be real with you,".
- Better: say the thing. If it needs a candor preface to land, it wasn't candid.

### Promotional / Marketing Language
Encyclopedic and technical writing is not ad copy. It should not read like a TV commercial transcript.

- Banned: "scenic", "breathtaking", "nestled in the heart of", "rich cultural heritage", "stunning natural beauty", "vibrant", "bustling", "state-of-the-art", "seamless", "treasure trove", "a must-see", "boasts".
- Better: specifics over adjectives. Not "a vibrant, bustling district," but "a district with roughly 200 restaurants and a Friday market."

### Editorializing Asides
Phrases that comment on the content instead of delivering it.

- Banned: "It's important to note that", "It's worth noting/mentioning", "Notably,", "Interestingly,", "Needless to say,", "It should be emphasized that", "Importantly,".
- Better: delete them. What follows is either worth stating plainly or not worth stating.

### Vague Attributions (Weasel Words)
Sourcing that implies authority without naming it.

- Banned: "some critics argue", "industry experts say", "many believe", "it is widely regarded", "observers have noted", "studies show" (with no study), "it has been said".
- Better: name the source or drop the claim. "A 2024 Pew survey found 62%..." If you can't attribute it, do not assert it.

### Inflated Vocabulary
Words overrepresented in model output and uncommon in plain writing.

- Demote on sight: delve, intricate, tapestry, pivotal, underscore, landscape (figurative), foster, testament, leverage (verb), robust, seamless, navigate (figurative), multifaceted, nuanced, holistic, realm, garner, myriad, plethora, crucial, profound, transformative, groundbreaking, revolutionary, comprehensive, elevate, harness, embark, showcase.
- Better: the plain word a domain expert would say out loud. Use not leverage, deep not profound, show not showcase, encourage not foster, area not realm.

### Rule of Three
Reflexive triplets, especially of adjectives.

- Banned: "innovative, transformative, and groundbreaking", "fast, reliable, and scalable".
- Better: one precise term, or two if both add information. "It cut latency by 40%," not "It was fast, efficient, and performant."

### False Range (From X to Y)
A construction that implies a spectrum where none exists.

- Banned: "from intimate gatherings to global movements", "from startups to enterprises", "from beginners to experts".
- Better: name the actual scope. "Used by teams of 5 to 50." If the endpoints are decorative, cut the frame.

### Negative Parallelism
"Not X, but Y" and variants, used for false drama.

- Banned: "It's not just a tool, it's a platform.", "This isn't a launch, it's a paradigm shift.", "more than just a...", "not only... but also...".
- Better: drop the setup, keep the claim. "It also schedules tasks." State what a thing is; don't stage a contrast to inflate it.

### Conjunction and Transition Overuse
Transition words used as filler with no real logical turn.

- Banned as openers when nothing turns: "Moreover,", "Furthermore,", "Additionally,", "In addition,", "Consequently,", "On the other hand,", "That being said,", "As such,", "In essence,", "Ultimately,".
- Better: start with the content. If a connection is real, make it explicit ("Because of that delay..."); if it's just sequence, no word is needed.

### Formulaic Section Structure
The same generic scaffold pasted onto any topic, regardless of fit.

- Banned section titles when they don't earn their place: "Challenges and Future Prospects", "Challenges and Future Directions", "Legacy and Interpretation", "Reception and Influence", "Impact and Legacy", "Conclusion".
- Better: let structure follow the material. Speculative "future prospects" sections are usually filler.

### Summary / Conclusion Reflex
The compulsion to restate what was just said.

- Banned: "In summary,", "Overall,", "In conclusion,", "To wrap up," followed by a paragraph that adds nothing.
- Better: end on the last substantive point. Add a closing line only if it carries new information.

---

## 2. Formatting and Style

### Boldface Spam
Bold is for the rare term that needs anchoring, not for emphasis on phrases the reader can already weigh.

- Banned: bolding a clause in most paragraphs; bolding the first words of every list item as a fake header.
- Better: plain prose. If everything is emphasized, nothing is.

### Lists Where Prose Belongs
Default to sentences. Use a list only when items are genuinely parallel and discrete (steps, specs, options).

- Banned: turning a flowing explanation into six one-fragment bullets.
- Better: write the paragraph. Reserve bullets for things that are actually enumerable.

### Title Case in Headings
Use title case with minor words lowercased: capitalize the first word, the last word, and all major words; lowercase articles (a, an, the), coordinating conjunctions (and, but, or, nor), and short prepositions (of, to, in, on, for). Do not capitalize every word, and do not use sentence case.

- Banned: "Key Features And Benefits" (start case, "And" wrongly capitalized), "key features and benefits" (sentence case).
- Better: "Key Features and Benefits".

### Em Dash Overuse
The em dash is fine occasionally; models lean on it as the default connector, overusing it where a comma, colon, or period would serve.

- Banned: two or more em-dash asides per paragraph; an em dash where a comma, colon, or period works.
- Better: prefer commas, colons, or splitting the sentence. Reserve the em dash for a genuine sharp aside.

### Curly / Smart Punctuation
Always use straight quotation marks and apostrophes. Never use curly (smart) ones, in any format, anywhere.

- Banned: any curly quote or curly apostrophe.
- Better: the straight `"` and `'` characters only.

---

## 3. Markup

Formatting artifacts from text generated in one markup system and pasted into another. Adapt to your target format.

### Markdown Leaking into the Wrong Context
Model output is markdown by default. When the destination is not markdown (wikitext, plain text, a CMS field, a code comment), the syntax should not survive.

- Banned in non-markdown targets: `## Heading` left as literal text, `**bold**` and `*italic*` asterisks, markdown bullet syntax pasted into a system that uses its own.
- Better: convert to the destination's real formatting, or strip it. Never ship raw markdown into a surface that does not render it.

### Horizontal Rule Overuse
Models pepper output with section dividers.

- Banned: a horizontal rule (`---`) between every short block; dividers used as decoration rather than to separate distinct sections.
- Better: use a divider only at a real structural break, sparingly or not at all.

### Stray Template / Placeholder Syntax
Host-specific template names in brackets can trigger unintended rendering.

- Banned: literal `{{...}}`, `[[...]]`, or other host markup dropped into prose where it was never meant to render.
- Better: remove host syntax that isn't yours to invoke, or escape it deliberately if you mean to show it as an example.

---

## 4. Citations and Sourcing

These are the most objective tells. Any one alone marks text as pasted from a chatbot.

### Reference-Tool Artifacts
- Banned, never under any circumstances: `:contentReference[oaicite:0]{index=0}`, `[oai_citation:1‡example.com]`, or any `oaicite` / `oai_citation` fragment. These are ChatGPT UI residue.
- Better: delete them. They are not citations.

### Tracking Parameters in URLs
- Banned: any URL ending in `utm_source=chatgpt.com` (or similar `utm_*=chatgpt` tags). This proves the link was lifted from a chatbot.
- Better: strip tracking parameters from every URL before using it.

### Reference-Cluster Artifacts
- Banned: "SourceName+3" fragments left inline, e.g. "ISO+3", "Google Cloud+3Microsoft Learn+3", "Wikipedia+1". These are grouped-source badges pasted as text.
- Better: remove them and cite the actual sources, one at a time.

### Fabricated or Unverifiable Citations
- Banned: plausible-looking references to papers, books, URLs, or authors you are not certain exist.
- Better: cite only what you can verify. If you can't, leave the claim unsupported and say so.

### Citations That Don't Support the Claim
- Banned: attaching a real source to a sentence it does not back up, on the assumption that a citation-shaped object is enough.
- Better: every citation must support the specific claim it is attached to. If it doesn't, find one that does or drop the claim.

### Broken or Future-Dated Citations
- Banned: malformed references; access or publication dates set in the future; multiple suspiciously uniform dates.
- Better: use real, correctly formatted dates and references. Flag anything you cannot confirm.

---

## 5. Text Addressed to the User (Chatbot Artifacts)

Conversational scraps that should never survive into a finished document.

### Sycophantic Openers
- Banned: "Certainly!", "Of course!", "Great question!", "Absolutely!", "Sure, here's...".
- Better: start with the content.

### Helpful-Assistant Closers
- Banned: "I hope this helps!", "Let me know if you need anything else!", "Feel free to ask!", "Happy to help!".
- Better: stop when the content is done.

### AI Self-Reference
- Banned: "As an AI language model...", "As a large language model, I...", "I cannot browse...", any sentence referring to itself as an AI.
- Better: none of this belongs in delivered prose.

### Refusals and Disclaimers Left Inline
- Banned: "I'm sorry, but I can't...", knowledge-cutoff hedges like "As of my last update," or "As of [date], based on available information,".
- Better: state the fact with its real date if relevant ("As of fiscal 2024..."), or omit the hedge.

### Response-Structuring Headers
- Banned: labels that are the AI organizing its reply, not part of the document: "Key facts needing addition or correction:", "Here's a breakdown:", "Summary of changes:", bracketed meta like "[Note: ...]".
- Better: deliver the content directly, without narrating the structure of your own answer.

### Unedited Placeholders
- Banned: "[Insert company name]", "[Your name here]", "[X]", "[add detail]", any bracketed fill-in left in the output.
- Better: fill them, or flag them to the user. Never ship a template with the blanks still in it.

---

## 6. Behavioral Tells (Collaborative and Editing Contexts)

These show up when generating commentary, edit rationales, or arguments, not just standalone prose.

### Overexplaining Why Things Matter
- Banned: the reflexive "This matters because...", followed by generic significance, ethics, or "broader implications" the context did not call for.
- Better: explain significance only when the reader needs it, and only with specifics.

### Self-Contradictory Hedging
- Banned: acknowledging a clear fact and immediately casting doubt on it ("The notice says X was the reason, but I'm trying to understand whether that was really the reason..."), or expressing uncertainty about information stated plainly in front of you.
- Better: if the information is clear, treat it as clear. Don't manufacture doubt to fill space.

### Selective Rule-Citing Against the Rule's Purpose
- Banned: quoting a policy, guideline, or precedent as cover for a conclusion the cited rule doesn't support, or supports the opposite of.
- Better: cite a rule only when it backs your point, and read it for what it says rather than what is convenient.

---

## Pre-Output Checklist

Run this on every draft. If any answer is yes, fix it.

1. Does the text assert that something is important or pivotal instead of showing it?
2. Any `-ing` clauses tailing sentences with vague claims of relevance or legacy?
3. Fake-candor openers (Honestly, And honestly?, Look, Here's the thing)?
4. Marketing or puffery vocabulary (scenic, vibrant, seamless, treasure trove, boasts)?
5. Editorializing throat-clearing ("it's worth noting", "notably", "importantly")?
6. Vague attributions with no named source ("experts say", "studies show")?
7. Inflated default words where a plain one fits (delve, leverage, foster, underscore, realm)?
8. Reflexive adjective triplets?
9. "From X to Y" ranges implying a nonexistent spectrum?
10. "Not just X, but Y" / "not only... but also" drama?
11. Filler transitions opening paragraphs (Moreover, Furthermore, On the other hand, As such)?
12. A generic section skeleton ("Challenges and Future Prospects") that doesn't fit the topic?
13. A summary paragraph that restates without adding?
14. Boldface or bullets where plain prose would do?
15. Headings in start case (every word capped) or sentence case instead of title case? Any curly quote or apostrophe?
16. Markdown syntax leaking into a non-markdown target? Stray `{{...}}` or `---` dividers?
17. Any `oaicite` / `oai_citation` fragment, `utm_source=chatgpt.com`, or "Source+3" cluster?
18. Fabricated citations, citations that don't support the claim, or future-dated references?
19. Chatbot scraps (Certainly!, I hope this helps, As an AI, response-structuring headers, [placeholders])?
20. "This matters because" overexplaining, manufactured doubt, or rule-citing that doesn't fit?

If it passes all of these and still reads like a person who knows the subject wrote it for a specific reader, it's done.