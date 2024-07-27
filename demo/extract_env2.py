import os
import subprocess
import sys

def save_env_variables(filename):
    with open(filename, 'w') as f:
        for key, value in os.environ.items():
            f.write(f'{key}={value}\n')

def load_env_variables(filename):
    env_vars = {}
    with open(filename, 'r') as f:
        for line in f:
            key, value = line.strip().split('=', 1)
            env_vars[key.upper()] = value
    return env_vars

def get_env_from_bat(bat_file):
    # Create a temporary batch file to capture the environment variables
    temp_bat = 'temp_capture_env.bat'
    with open(temp_bat, 'w') as f:
        f.write(f'@echo off\n')
        f.write(f'call "{bat_file}"\n')
        f.write(f'set > env_after.txt\n')

    # Run the temporary batch file
    subprocess.call(['cmd.exe', '/c', temp_bat])

    # Read the captured environment variables from env_after.txt
    env_vars = load_env_variables('env_after.txt')

    # Clean up the temporary files
    os.remove(temp_bat)
    os.remove('env_after.txt')

    return env_vars

def compare_env_vars(before, after):
    new_vars = {}
    for key in after:
        if key not in before:
            new_vars[key] = after[key]
        elif key == 'Path':
            before_paths = set(before[key].split(';'))
            after_paths = set(after[key].split(';'))
            new_paths = after_paths - before_paths
            if new_paths:
                new_vars[key] = ';'.join(new_paths)
        elif before[key] != after[key]:
            new_vars[key] = after[key]
    return new_vars

def create_fast_bat(new_vars, filename):
    with open(filename, 'w') as f:
        f.write('@echo off\n')
        for key, value in new_vars.items():
            if key == 'Path':
                f.write(f'set {key}=%{key}%;{value}\n')
            else:
                f.write(f'set {key}={value}\n')

def main():
    if len(sys.argv) != 3:
        print("Usage: python script.py <slow.bat> <fast.bat>")
        sys.exit(1)
    
    slow_bat = sys.argv[1]
    fast_bat = sys.argv[2]

    # Save current environment variables to before.txt
    save_env_variables('before.txt')
    
    # Get environment variables after running the slow.bat file
    after_vars = get_env_from_bat(slow_bat)
    
    # Save environment variables to after.txt
    with open('after.txt', 'w') as f:
        for key, value in after_vars.items():
            f.write(f'{key}={value}\n')
    
    # Load environment variables from before.txt
    before_vars = load_env_variables('before.txt')
    
    # Compare and find new/modified environment variables
    new_vars = compare_env_vars(before_vars, after_vars)
    
    # Create the specified fast.bat file with new/modified environment variables
    create_fast_bat(new_vars, fast_bat)

if __name__ == '__main__':
    main()
