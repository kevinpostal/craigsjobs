f=open('/home/tube/django_projects/craigsjobs/data.json', 'r')

print '['

x = 0

for line in f:
    
    split = line.split('~'),

    print '{'
    print '"pk": ' + x.__str__() + ','
    print '"model": "sites.site",'
    print '"fields": {'
    print '"domain": " ' + split[0][0] + ' ", '
    print '"name": "' + split[0][1].capitalize().rstrip() + '"'
    print '}'
    print '},'    
    x = x + 1
    
f.close()

print ']'












