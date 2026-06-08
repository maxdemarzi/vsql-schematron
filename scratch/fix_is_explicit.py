import re

filepath = "/home/maxdemarzi/vsql-schematron/src/domain_specific_matching.cc"
with open(filepath, 'r', encoding='utf-8') as f:
    content = f.read()

# Replace rel.is_explicit = true; with rel.is_explicit = false; inside matchDomainSpecificKeys
# We can find the definition of matchDomainSpecificKeys and replace within it.
pattern = r"(bool matchDomainSpecificKeys\(.*?\n\})(?=\s*//|\s*bool|\s*void|\s*$)"

def replacer(match):
    body = match.group(1)
    # Replace all rel.is_explicit = true; with rel.is_explicit = false;
    new_body = body.replace("rel.is_explicit = true;", "rel.is_explicit = false;")
    return new_body

new_content, count = re.subn(pattern, replacer, content, flags=re.DOTALL)
print(f"Substitutions made: {count}")

with open(filepath, 'w', encoding='utf-8') as f:
    f.write(new_content)
