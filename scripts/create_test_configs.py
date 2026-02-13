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
    
    # Create configs directory
    config_dir = "src/configs"
    os.makedirs(config_dir, exist_ok=True)
    print(f"Created directory: {config_dir}")
    
    # Verify directory exists
    if not os.path.exists(config_dir):
        print(f"❌ ERROR: Directory {config_dir} was not created!")
        print(f"Current directory contents:")
        print(os.listdir('.'))
        if os.path.exists('src'):
            print(f"src/ contents: {os.listdir('src')}")
        sys.exit(1)
    
    print(f"✅ Verified: {os.path.abspath(config_dir)} exists")
    
    # SQL configuration (points to localhost test database)
    sql_config = {
        "connections": 5,
        "dbname": "letovo_test",
        "host": "localhost",
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
        filepath = os.path.join(config_dir, filename)
        abs_filepath = os.path.abspath(filepath)
        
        # Debug: show what we're trying to write
        print(f"Attempting to create: {abs_filepath}")
        
        # Verify parent directory exists
        parent_dir = os.path.dirname(abs_filepath)
        if not os.path.exists(parent_dir):
            print(f"❌ Parent directory does not exist: {parent_dir}")
            sys.exit(1)
        
        # Write the file
        try:
            with open(abs_filepath, "w") as f:
                json.dump(config_data, f, indent=2)
            print(f"✅ Created: {filepath}")
        except Exception as e:
            print(f"❌ Failed to create {filepath}: {e}")
            print(f"   Parent dir exists: {os.path.exists(parent_dir)}")
            print(f"   Parent dir writable: {os.access(parent_dir, os.W_OK)}")
            raise
    
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
