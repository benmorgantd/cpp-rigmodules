"""The RigModules Python package: thin wrappers around the plugin's commands (D24).

It imports no Qt and no bmaya, and reads no environment variables or asset paths, so it works in a bare
mayapy with only the plugin loaded. Pipeline code (mayadev) reaches rig internals only through it.

    import rigmodules.query
    info = rigmodules.query.get_rig_info("hero_body:arm_ctrl_l")
    info.asset_id, info.rig_name                               # "shooter\\main\\characters\\hero", "body"
"""
