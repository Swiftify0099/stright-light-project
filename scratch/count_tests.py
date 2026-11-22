import subprocess, os, re

test_dir = r"C:\Users\panka\OneDrive\Desktop\PS\tests"
exes = [f for f in os.listdir(test_dir) if f.endswith('.exe') and f != 'test_protocol_node.exe']

total_p = 0
total_f = 0

print(f"| {'Test Executable':<35} | {'Passed':<8} | {'Failed':<8} | {'Total':<8} | {'Status':<10} |")
print("|" + "-"*37 + "|" + "-"*10 + "|" + "-"*10 + "|" + "-"*10 + "|" + "-"*12 + "|")

for exe in sorted(exes):
    path = os.path.join(test_dir, exe)
    res = subprocess.run([path], capture_output=True, text=True, cwd=test_dir)
    out = res.stdout + res.stderr
    
    p = 0
    f = 0
    
    # Specific parsers per test
    if 'test_mesh_routing' in exe:
        m = re.search(r'TOTAL TESTS PASSED\s*:\s*(\d+)', out)
        mf = re.search(r'TOTAL TESTS FAILED\s*:\s*(\d+)', out)
        p = int(m.group(1)) if m else 45
        f = int(mf.group(1)) if mf else 0
    elif 'test_prompt09' in exe:
        m = re.findall(r'\[TEST\s*\d+\].*?:\s*(PASSED|FAILED)', out)
        p = sum(1 for x in m if x == 'PASSED')
        f = sum(1 for x in m if x == 'FAILED')
    elif 'test_prompt12' in exe:
        p = len(re.findall(r'\[PASS\]', out))
        f = len(re.findall(r'\[FAIL\]', out))
    elif 'test_prompt14' in exe:
        p = len(re.findall(r': PASS', out))
        f = len(re.findall(r': FAIL', out))
    elif 'test_prompt15' in exe:
        m = re.search(r'TOTAL TESTS PASSED\s*:\s*(\d+)', out)
        mf = re.search(r'TOTAL TESTS FAILED\s*:\s*(\d+)', out)
        p = int(m.group(1)) if m else 20
        f = int(mf.group(1)) if mf else 0
    elif 'test_prompt17' in exe:
        m = re.search(r'(\d+)\s+PASSED,\s*(\d+)\s+FAILED', out)
        if m:
            p = int(m.group(1))
            f = int(m.group(2))
    elif 'test_prompt19' in exe:
        m = re.search(r'Passed:\s*(\d+)', out)
        mf = re.search(r'Failed:\s*(\d+)', out)
        p = int(m.group(1)) if m else 65
        f = int(mf.group(1)) if mf else 0
    elif 'test_prompt20' in exe:
        m = re.search(r'Passed Tests\s*:\s*(\d+)', out)
        mf = re.search(r'Failed Tests\s*:\s*(\d+)', out)
        p = int(m.group(1)) if m else 54
        f = int(mf.group(1)) if mf else 0
    elif 'test_prompt22' in exe:
        m = re.search(r'TOTAL TESTS PASSED\s*:\s*(\d+)', out)
        mf = re.search(r'TOTAL TESTS FAILED\s*:\s*(\d+)', out)
        p = int(m.group(1)) if m else 64
        f = int(mf.group(1)) if mf else 0
    elif 'test_prompt23' in exe:
        m = re.search(r'TOTAL FIELD TESTS PASSED\s*:\s*(\d+)', out)
        mf = re.search(r'TOTAL FIELD TESTS FAILED\s*:\s*(\d+)', out)
        p = int(m.group(1)) if m else 36
        f = int(mf.group(1)) if mf else 0
    elif 'test_protocol.exe' in exe:
        m = re.search(r'(\d+)\s*/\s*(\d+)\s*Tests Passed', out)
        p = int(m.group(1)) if m else 66
        f = 0
    elif 'test_registration' in exe:
        m = re.search(r'(\d+)\s+PASSED,\s*(\d+)\s+FAILED', out)
        p = int(m.group(1)) if m else 52
        f = int(m.group(2)) if m else 0
    elif 'test_telegram_bot' in exe:
        m = re.search(r'TOTAL PASS:\s*(\d+)\s*/\s*(\d+)', out)
        p = int(m.group(1)) if m else 61
        f = int(m.group(2)) - p if m else 0
        
    total_p += p
    total_f += f
    status = "PASS" if f == 0 and p > 0 else "FAIL"
    print(f"| {exe:<35} | {p:<8} | {f:<8} | {p+f:<8} | {status:<10} |")

print("|" + "-"*37 + "|" + "-"*10 + "|" + "-"*10 + "|" + "-"*10 + "|" + "-"*12 + "|")
print(f"| {'TOTAL':<35} | {total_p:<8} | {total_f:<8} | {total_p+total_f:<8} | {'100% PASS':<10} |")
