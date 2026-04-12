proc sync_dir_children {src_dir dst_dir} {
    if {![file isdirectory $src_dir]} {
        return
    }
    file mkdir $dst_dir
    foreach item [glob -nocomplain -directory $src_dir *] {
        set base [file tail $item]
        set dst_item [file join $dst_dir $base]
        if {[file exists $dst_item]} {
            file delete -force $dst_item
        }
        file copy -force $item $dst_item
    }
}

proc xci_files_in_dir {ip_dir} {
    set xcis {}
    if {![file isdirectory $ip_dir]} {
        return $xcis
    }

    foreach inst_dir [glob -nocomplain -types d -directory $ip_dir *] {
        foreach xci [glob -nocomplain -types f -directory $inst_dir *.xci] {
            lappend xcis $xci
        }
    }
    return $xcis
}

proc normalize_xci_shareddir {ip_dir} {
    if {![file isdirectory $ip_dir]} {
        return 0
    }

    set rewritten 0
    foreach xci [xci_files_in_dir $ip_dir] {
        if {[catch {set fh [open $xci r]}]} {
            continue
        }
        set content [read $fh]
        close $fh

        set original $content
        regsub -all {("SHAREDDIR"[ \t\r\n]*:[ \t\r\n]*\[[ \t\r\n]*\{[ \t\r\n]*"value"[ \t\r\n]*:[ \t\r\n]*")[^"]*("[ \t\r\n]*\}[ \t\r\n]*\],?)} $content {\1../../ipshared\2} content

        if {$content ne $original} {
            set out [open $xci w]
            puts -nonewline $out $content
            close $out
            incr rewritten
        }
    }

    return $rewritten
}

proc repair_bd_ip_snapshot {repo_root project_name bd_name} {
    set bd_dir [file join $repo_root src bd $bd_name]
    set src_ip_dir [file join $bd_dir ip]
    set src_shared_dir [file join $bd_dir ipshared]
    set local_bd_dir [file join $repo_root ${project_name}.srcs sources_1 bd $bd_name]
    set local_gen_bd_dir [file join $repo_root ${project_name}.gen sources_1 bd $bd_name]
    set local_ip_dir [file join $local_bd_dir ip]
    set local_shared_dir [file join $local_gen_bd_dir ipshared]

    if {[file isdirectory $local_ip_dir]} {
        sync_dir_children $local_ip_dir $src_ip_dir
    }
    if {[file isdirectory $local_shared_dir]} {
        sync_dir_children $local_shared_dir $src_shared_dir
    }

    normalize_xci_shareddir $src_ip_dir
    normalize_xci_shareddir $local_ip_dir
}
