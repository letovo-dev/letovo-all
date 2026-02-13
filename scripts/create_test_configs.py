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
        with open(filepath, "w") as f:
            json.dump(config_data, f, indent=2)
        print(f"✅ Created: {filepath}")
    
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
