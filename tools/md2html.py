# SPDX-License-Identifier: AGPL-3.0-only
import markdown, sys, pathlib
src=pathlib.Path(sys.argv[1]); out=pathlib.Path(sys.argv[2])
html=markdown.markdown(src.read_text(), extensions=['tables','fenced_code'])
css='''body{font-family:"Noto Sans CJK SC","Noto Sans SC","WenQuanYi Micro Hei",sans-serif;font-size:10.5pt;line-height:1.5;margin:0 6mm}
table{border-collapse:collapse;margin:8px 0;font-size:9pt}td,th{border:1px solid #999;padding:3px 6px;vertical-align:top}th{background:#eee}
img{max-width:100%;page-break-inside:avoid}h1{font-size:18pt}h2{font-size:14pt;border-bottom:1px solid #ccc;margin-top:18px}code{background:#f4f4f4;font-size:9pt}
@page{size:A4;margin:14mm}'''
out.write_text(f'<!doctype html><html><head><meta charset="utf-8"><style>{css}</style></head><body>{html}</body></html>')
