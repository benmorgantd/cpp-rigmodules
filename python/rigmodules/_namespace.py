"""Namespace text shared by the package's scene queries. Private: tools pass namespaces, they don't build patterns."""


def get_node_pattern(namespace: str) -> str:
    """The cmds.ls pattern for every node directly in namespace: ":hero:*", or ":*" for the root ("" or ":").
    Colons around the namespace are dropped, so ":hero", "hero:" and "hero" mean the same."""
    name = namespace.strip(":")
    return f":{name}:*" if name else ":*"
