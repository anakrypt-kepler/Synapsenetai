#!/usr/bin/env python3
import os
import sys
import hashlib
import time
from pathlib import Path

def sha256_file(filepath, chunk_size=8192):
    """Calculate SHA256 checksum of a file"""
    sha256 = hashlib.sha256()
    with open(filepath, 'rb') as f:
        for chunk in iter(lambda: f.read(chunk_size), b''):
            sha256.update(chunk)
    return sha256.hexdigest()

def download_with_retries(url, output_file, max_retries=3, timeout=30, headers=None):
    """Download file with retry logic and resume support"""
    import requests
    
    headers = headers or {}
    output_file = Path(output_file)
    
    # Check if file already exists and try to resume
    resume_headers = dict(headers)
    if output_file.exists():
        existing_size = output_file.stat().st_size
        resume_headers['Range'] = f'bytes={existing_size}-'
    
    for attempt in range(max_retries):
        try:
            print(f"Download attempt {attempt + 1}/{max_retries}: {url}")
            response = requests.get(url, stream=True, headers=resume_headers, timeout=timeout)
            
            if response.status_code in [200, 206]:  # 206 = Partial Content (resume)
                output_file.parent.mkdir(parents=True, exist_ok=True)
                mode = 'ab' if response.status_code == 206 else 'wb'
                
                total_size = int(response.headers.get('content-length', 0))
                downloaded = 0
                
                with open(output_file, mode) as f:
                    for chunk in response.iter_content(chunk_size=1024*1024):
                        if chunk:
                            f.write(chunk)
                            downloaded += len(chunk)
                            if total_size > 0:
                                percent = (downloaded / total_size) * 100
                                print(f"  Progress: {percent:.1f}% ({downloaded}/{total_size} bytes)", end='\r')
                
                print()  # New line after progress
                return True
            elif response.status_code == 404:
                print(f"  File not found (404): {url}")
                return False
            else:
                print(f"  HTTP {response.status_code}, retrying...")
        except (requests.RequestException, TimeoutError) as e:
            print(f"  Error: {e}")
            if attempt < max_retries - 1:
                wait_time = 2 ** attempt  # Exponential backoff
                print(f"  Retrying in {wait_time}s...")
                time.sleep(wait_time)
    
    print(f"Failed to download after {max_retries} attempts")
    return False

def main():
    import argparse
    p = argparse.ArgumentParser(description="Download GGUF models from Hugging Face")
    p.add_argument("model", help="huggingface model id (e.g., 'meta-llama/Llama-2-7b-hf')")
    p.add_argument("--dest", default=str(Path.home() / ".synapsenet/models"))
    p.add_argument("--token", default=os.getenv("HUGGINGFACEHUB_API_TOKEN") or os.getenv("HUGGINGFACE_TOKEN") or "")
    p.add_argument("--retries", type=int, default=3, help="Number of download retries")
    p.add_argument("--verify", action="store_true", help="Verify checksum after download (if available)")
    args = p.parse_args()
    
    dest = Path(args.dest)
    dest.mkdir(parents=True, exist_ok=True)
    token = args.token or None
    
    try:
        from huggingface_hub import list_repo_files, hf_hub_download
        
        print(f"Listing files from {args.model}...")
        files = list_repo_files(args.model, repo_type="model", token=token)
        ggufs = [f for f in files if f.endswith('.gguf')]
        
        if not ggufs:
            print("ERROR: No .gguf files found in repository:", args.model)
            sys.exit(1)
        
        for f in ggufs:
            print(f"\nDownloading: {f}")
            try:
                local = hf_hub_download(repo_id=args.model, filename=f, repo_type="model", 
                                       token=token, local_dir=str(dest))
                print(f"✓ Saved: {local}")
            except Exception as e:
                print(f"✗ Failed to download {f}: {e}")
                sys.exit(1)
    except Exception as e:
        # Fallback: direct download from HuggingFace
        print(f"Using fallback download method: {e}\n")
        
        import requests
        base = f"https://huggingface.co/{args.model}/resolve/main/"
        candidates = ["model.gguf", "pytorch_model.gguf", "ggml-model.gguf"]
        
        headers = {"Authorization": f"Bearer {token}"} if token else {}
        
        for fname in candidates:
            url = base + fname
            output_path = dest / fname
            
            if download_with_retries(url, output_path, max_retries=args.retries, headers=headers):
                print(f"✓ Successfully downloaded to: {output_path}")
                
                # Optional checksum verification
                if args.verify:
                    print("Calculating SHA256 checksum...")
                    checksum = sha256_file(output_path)
                    checksum_file = output_path.with_suffix('.sha256')
                    checksum_file.write_text(checksum)
                    print(f"  Checksum: {checksum}")
                    print(f"  Saved to: {checksum_file}")
                
                return
        
        print("ERROR: Download failed for all candidates")
        sys.exit(1)

if __name__ == "__main__":
    main()
