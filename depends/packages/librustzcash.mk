package=librustzcash
$(package)_version=0.1
$(package)_download_path=https://github.com/zcash/$(package)/archive/
$(package)_file_name=$(package)-$($(package)_git_commit).tar.gz
$(package)_download_file=$($(package)_git_commit).tar.gz
$(package)_sha256_hash=146ba08f2e325d569ec9e724474911a9bd66192b841bb08808f7f67b9bb320fb
$(package)_git_commit=37f20fb90c856e82634c2eabb6bb3f754f3b350e
$(package)_dependencies=rust $(rust_crates)
$(package)_patches=cargo.config

define $(package)_preprocess_cmds
  mkdir .cargo && \
  cat $($(package)_patch_dir)/cargo.config | sed 's|CRATE_REGISTRY|$(host_prefix)/$(CRATE_REGISTRY)|' > .cargo/config
endef

define $(package)_build_cmds
  cargo build --frozen --release
endef

define $(package)_stage_cmds
  mkdir $($(package)_staging_dir)$(host_prefix)/lib/ && \
  mkdir $($(package)_staging_dir)$(host_prefix)/include/ && \
  cp target/release/librustzcash.a $($(package)_staging_dir)$(host_prefix)/lib/ && \
  cp include/librustzcash.h $($(package)_staging_dir)$(host_prefix)/include/
endef
