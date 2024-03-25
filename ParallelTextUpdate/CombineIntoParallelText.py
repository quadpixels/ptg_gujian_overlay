# -*- coding: utf-8 -*-

import sys
import csv
from xml.etree.ElementTree import Element, SubElement, tostring, ElementTree, parse


def GetXML(sourceFilename):
  uniqueColnames = []
  items = [] # List of dicts
  srcXML = parse(sourceFilename)

  srcXMLRoot = srcXML.getroot()
  tableRoot = srcXMLRoot.find('Select')
  
  for item in tableRoot:
    childrenCount = len(list(item)) # To not trigger DeprecationWarning
    theItem = dict()
    for i in range(0, childrenCount): # [0] is supposed to be used as a Primary Key
      itemColumnId = item[i].get('Column')
      if itemColumnId not in uniqueColnames: uniqueColnames.append(itemColumnId)
      itemValue = item[i].text
      if (itemValue is None):
        itemValue = ''
      else: itemValue = itemValue.strip()
      theItem[itemColumnId] = itemValue
    items.append(theItem)
  return items

eng = GetXML(sys.argv[1])
chs = GetXML(sys.argv[2])

assert(len(eng) == len(chs))

with open(sys.argv[3], 'w', newline='') as out:
  outwriter = csv.writer(out, delimiter='\t', quotechar='"', quoting=csv.QUOTE_MINIMAL)
  outwriter.writerow(["ID","Character","CHS", "ENG Character", "ENG REV"])
  for i in range(len(eng)):
    entry_eng = eng[i]; entry_chs = chs[i];
    outwriter.writerow([entry_eng['DZ00'], entry_chs['DZ04'], entry_chs['DZ05'], entry_eng['DZ04'], entry_eng['DZ05']])
