#!/usr/bin/env python3
"""
Creates test configuration files for CI/CD workflow.
This script creates the necessary JSON config files for the server to run in test mode.
"""

import json
import os
import sys

def main():
    # Get workspace directory (GitHub Actions sets this)
    workspace = os.environ.get('GITHUB_WORKSPACE', os.getcwd())
    print(f"Workspace: {workspace}")
    
    # Create workspace directory if it doesn't exist (for local testing)
    os.makedirs(workspace, exist_ok=True)
    
    # Change to workspace directory
    os.chdir(workspace)
    print(f"Working directory: {os.getcwd()}")
    
    # Check if src directory exists
    if not os.path.exists("src"):
        print(f"❌ ERROR: src/ directory does not exist in {os.getcwd()}")
        print(f"Directory contents: {os.listdir('.')}")
        sys.exit(1)
    
    # Create configs directory with absolute path
    config_dir = os.path.join(os.getcwd(), "src", "configs")
    os.makedirs(config_dir, exist_ok=True)
    print(f"Created directory: {config_dir}")
    
    # Verify directory exists and show details
    if not os.path.exists(config_dir):
        print(f"❌ ERROR: Directory {config_dir} was not created!")
        sys.exit(1)
    
    print(f"✅ Verified: {config_dir} exists")
    print(f"   Absolute path: {os.path.abspath(config_dir)}")
    print(f"   Is directory: {os.path.isdir(config_dir)}")
    print(f"   Writable: {os.access(config_dir, os.W_OK)}")
    print(f"   Contents: {os.listdir(config_dir)}")
    
    # SQL configuration (points to localhost test database)
    # Use 127.0.0.1 instead of localhost for GitHub Actions compatibility
    sql_config = {
        "connections": 5,
        "dbname": "letovo_test",
        "host": "127.0.0.1",
        "password": "postgres",
        "user": "postgres",
        "port": 5432
    }
    
    # Server configuration
    server_config = {
        "adress": "0.0.0.0",
        "port": 8080,
        "thread_pool_size": 2,
        "certs_path": "./certs",
        "update_hashes": False
    }
    
    # Pages configuration  
    pages_config = {
        "wiki_path": "/tmp/wiki",
        "user_avatars_path": "/tmp/avatars",
        "admin_avatars_path": "/tmp/admin_avatars",
        "achivements_path": "/tmp/achievements",
        "media_path": "/tmp/media",
        "secret_example_path": "/tmp/secrets",
        "media_cache_size": 10,
        "create_file": False
    }
    
    # Market configuration
    market_config = {
        "bid_resolver_sleep_time": 1000,
        "junk_user": "system"
    }
    
    # Write configuration files
    configs = {
        "SqlConnectionConfig.json": sql_config,
        "ServerConfig.json": server_config,
        "PagesConfig.json": pages_config,
        "MarketConfig.json": market_config
    }
    
    for filename, config_data in configs.items():
        # Use absolute path from the start
        filepath = os.path.join(config_dir, filename)
        
        # Debug: show what we're trying to write
        print(f"Attempting to create: {filepath}")
        
        # Write the file
        try:
            # Try creating a test file first
            test_path = os.path.join(config_dir, ".test")
            with open(test_path, "w") as f:
                f.write("test")
            os.remove(test_path)
            
            # Now write the actual config
            with open(filepath, "w") as f:
                json.dump(config_data, f, indent=2)
            
            # Verify file was actually created
            if os.path.exists(filepath):
                print(f"✅ Created: {filename}")
            else:
                print(f"❌ File not found after write: {filepath}")
                sys.exit(1)
        except Exception as e:
            print(f"❌ Failed to create {filename}: {e}")
            print(f"   Attempted path: {filepath}")
            print(f"   Config dir: {config_dir}")
            print(f"   Config dir exists: {os.path.exists(config_dir)}")
            print(f"   Config dir is dir: {os.path.isdir(config_dir)}")
            import traceback
            traceback.print_exc()
            sys.exit(1)
    
    print(f"\n✅ Successfully created {len(configs)} configuration files")
    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as e:
        print(f"❌ Error: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)
